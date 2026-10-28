#include "Crafting.h"

#include "Config.h"
#include "Forms.h"

namespace
{
	constexpr std::size_t  kPageSize = 8;      // entries per message box before "More..."
	constexpr std::uint8_t kButtonOffset = 4;  // added by the engine to the pressed button index

	void Notify(const std::string& a_msg)
	{
		logger::info("{}", a_msg);
		if (Config::printToConsole) {
			if (auto* console = RE::ConsoleLog::GetSingleton()) {
				console->Print("[Fire Pots] %s", a_msg.c_str());
			}
		}
		RE::SendHUDMessage::ShowHUDMessage(a_msg.c_str());
	}

	bool HasName(const RE::TESForm* a_form)
	{
		const char* n = a_form ? a_form->GetName() : nullptr;
		return n && *n;
	}

	std::string NameOf(const RE::TESForm* a_form)
	{
		const char* n = a_form ? a_form->GetName() : nullptr;
		return n && *n ? n : fmt::format("{:08X}", a_form ? a_form->GetFormID() : 0u);
	}

	using Inv = std::map<RE::TESBoundObject*, int>;

	Inv Inventory(RE::PlayerCharacter* a_player)
	{
		Inv out;
		for (const auto& [obj, data] : a_player->GetInventory()) {
			if (obj && data.first > 0) {
				out[obj] += data.first;
			}
		}
		return out;
	}

	int Have(const Inv& a_inv, RE::TESBoundObject* a_obj)
	{
		const auto it = a_inv.find(a_obj);
		return it == a_inv.end() ? 0 : it->second;
	}

	RE::TESBoundObject* Resolve(const std::string& a_ref)
	{
		auto* form = Forms::Lookup(a_ref);
		auto* obj = form ? form->As<RE::TESBoundObject>() : nullptr;
		return obj && HasName(obj) ? obj : nullptr;  // nameless items can't be picked up, so never carried
	}

	// Every configured container, in order (the first ones you carry get used first).
	std::vector<RE::TESBoundObject*> Containers()
	{
		std::vector<RE::TESBoundObject*> out;
		for (const auto& ref : Config::containers) {
			if (auto* obj = Resolve(ref); obj && std::ranges::find(out, obj) == out.end()) {
				out.push_back(obj);
			}
		}
		return out;
	}

	int ContainersHeld(const Inv& a_inv)
	{
		int n = 0;
		for (auto* c : Containers()) {
			n += Have(a_inv, c);
		}
		return n;
	}

	struct Part
	{
		RE::TESBoundObject* form = nullptr;
		int                 count = 1;
	};

	std::vector<Part> TriggerParts(int a_trigger)
	{
		std::vector<Part> out;
		for (const auto& i : Config::triggerIngredients) {
			if (i.group == a_trigger) {
				if (auto* obj = Resolve(i.form)) {
					out.push_back({ obj, i.count });
				}
			}
		}
		return out;
	}

	std::vector<Part> Fillings(int a_payload)
	{
		std::vector<Part> out;
		for (const auto& i : Config::fillings) {
			if (i.group == a_payload) {
				if (auto* obj = Resolve(i.form); obj && std::ranges::none_of(out, [&](const Part& p) { return p.form == obj; })) {
					out.push_back({ obj, i.count });
				}
			}
		}
		return out;
	}

	// What one bomb takes (besides a container): the trigger's parts and the filling, times the
	// strength (mild 1x, moderate 2x, major 3x), with the same item merged (Fire Salts twice).
	std::vector<Part> Recipe(int a_trigger, const Part& a_filling, int a_strength)
	{
		std::vector<Part> out;
		auto add = [&](RE::TESBoundObject* a_form, int a_count) {
			for (auto& p : out) {
				if (p.form == a_form) {
					p.count += a_count;
					return;
				}
			}
			out.push_back({ a_form, a_count });
		};
		const int mult = a_strength + 1;
		for (const auto& p : TriggerParts(a_trigger)) {
			add(p.form, p.count * mult);
		}
		add(a_filling.form, a_filling.count * mult);
		return out;
	}

	int CanMake(const Inv& a_inv, const std::vector<Part>& a_recipe)
	{
		int n = ContainersHeld(a_inv);
		for (const auto& p : a_recipe) {
			n = (std::min)(n, Have(a_inv, p.form) / p.count);
		}
		return (std::max)(n, 0);
	}

	std::string Describe(const std::vector<Part>& a_parts, const Inv* a_inv)
	{
		std::string s;
		for (const auto& p : a_parts) {
			if (!s.empty()) {
				s += ", ";
			}
			s += p.count > 1 ? fmt::format("{} {}", p.count, NameOf(p.form)) : NameOf(p.form);
			if (a_inv) {
				s += fmt::format(" (have {})", Have(*a_inv, p.form));
			}
		}
		return s;
	}

	// Things you can use first, the rest after (stable, so the INI order holds within each part).
	template <class T, class Usable>
	void UsableFirst(std::vector<T>& a_list, Usable&& a_usable)
	{
		std::ranges::stable_partition(a_list, a_usable);
	}

	// --- A message box with a list of choices --------------------------------------------
	// onPick gets the chosen entry's index into entries, or -1 for Back.
		struct ListBox
	{
		std::string              title;
		std::vector<std::string> entries;
		bool                     back = false;
		std::function<void(int)> onPick;
		std::size_t              page = 0;
	};

	class Callback final : public RE::IMessageBoxCallback
	{
	public:
		explicit Callback(std::function<void(std::uint8_t)> a_fn) :
			fn_(std::move(a_fn)) {}

		void Run(std::uint8_t a_button) override
		{
			// Run the follow-up after the message box has closed.
			auto fn = fn_;
			SKSE::GetTaskInterface()->AddTask([fn, a_button]() { fn(a_button); });
		}

	private:
		std::function<void(std::uint8_t)> fn_;
	};

	void Show(std::shared_ptr<ListBox> a_box)
	{
		const std::size_t first = a_box->page * kPageSize;
		const std::size_t last = (std::min)(a_box->entries.size(), first + kPageSize);
		const bool        more = a_box->entries.size() > kPageSize;

		// buttons: entries on this page, [More...], [Back], Cancel
		std::vector<std::string> labels;
		std::vector<int>         action;  // >= 0 entry index, -1 back, -2 more, -3 cancel
		for (std::size_t i = first; i < last; ++i) {
			labels.push_back(a_box->entries[i]);
			action.push_back(static_cast<int>(i));
		}
		if (more) {
			labels.push_back("More...");
			action.push_back(-2);
		}
		if (a_box->back) {
			labels.push_back("Back");
			action.push_back(-1);
		}
		labels.push_back("Cancel");
		action.push_back(-3);

		auto* data = RE::UIMessageDataFactory::Create<RE::MessageBoxData>();
		if (!data) {
			logger::error("Could not create a message box");
			return;
		}
		data->bodyText = a_box->title.c_str();
		for (const auto& l : labels) {
			data->buttonText.push_back(RE::BSString(l.c_str()));
		}
		data->warningType = 10;
		data->buttonPressOffset = kButtonOffset;
		data->verticalButtons = true;
		data->isCancellable = true;  // Escape / Tab = Cancel
		data->cancelButtonIndex = static_cast<std::int32_t>(labels.size() - 1);
		data->callback = RE::BSTSmartPointer<RE::IMessageBoxCallback>(new Callback([a_box, action](std::uint8_t a_button) {
			const int idx = static_cast<int>(a_button) - kButtonOffset;
			if (idx < 0 || idx >= static_cast<int>(action.size())) {
				return;  // cancelled
			}
			switch (const int act = action[idx]) {
			case -3:
				return;
			case -2:
				a_box->page = (a_box->page + 1) * kPageSize < a_box->entries.size() ? a_box->page + 1 : 0;
				Show(a_box);
				return;
			case -1:
				a_box->onPick(-1);
				return;
			default:
				a_box->onPick(act);
				return;
			}
		}));
		RE::MessageBoxMenu::QueueMessage(data);
	}

	// --- The steps: trigger -> payload -> (filling) -> strength -> amount -------------------
	void ChooseTrigger();
	void ChoosePayload(int a_trigger);
	void ChooseFilling(int a_trigger, int a_payload);
	void ChooseStrength(int a_trigger, int a_payload, Part a_filling);

	void Make(int a_bomb, const std::vector<Part>& a_recipe, int a_wanted)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto& type = Forms::Types()[a_bomb];
		if (!player || !type.Ready()) {
			return;
		}
		// count again: the inventory may have changed while the menu was up
		const auto inv = Inventory(player);
		const int  n = (std::min)(a_wanted, CanMake(inv, a_recipe));
		if (n <= 0) {
			Notify("You no longer have what that takes.");
			return;
		}
		int left = n;
		for (auto* c : Containers()) {
			const int take = (std::min)(left, Have(inv, c));
			if (take > 0) {
				player->RemoveItem(c, take, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				left -= take;
			}
		}
		for (const auto& p : a_recipe) {
			player->RemoveItem(p.form, p.count * n, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
		}
		player->AddObjectToContainer(type.item, nullptr, n, nullptr);
		RE::PlaySound("UIAlchemyCreatePotion");
		Notify(n == 1 ? fmt::format("Made a {}", type.name) : fmt::format("Made {} x {}", n, type.name));
	}

	void ChooseAmount(int a_trigger, int a_payload, Part a_filling, int a_strength)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		const int  bomb = Bombs::Index(a_trigger, a_payload, a_strength);
		const auto recipe = Recipe(a_trigger, a_filling, a_strength);
		const auto inv = Inventory(player);
		const int  max = CanMake(inv, recipe);
		const auto& name = Forms::Types()[bomb].name;

		auto box = std::make_shared<ListBox>();
		box->back = true;
		std::vector<int> amounts;
		if (max <= 0) {
			// just information: what it takes and what's missing
			box->title = fmt::format("A {} takes an empty container (you have {}) and {}.", name, ContainersHeld(inv),
				Describe(recipe, &inv));
		} else if (max == 1) {
			Make(bomb, recipe, 1);
			return;
		} else {
			amounts.push_back(1);
			if (max > 5) {
				amounts.push_back(5);
			}
			amounts.push_back(max);
			box->title = fmt::format("How many {}? (up to {})", name, max);
			for (const int a : amounts) {
				box->entries.push_back(a == max ? fmt::format("All ({})", a) : std::to_string(a));
			}
		}
		box->onPick = [=](int i) {
			if (i < 0) {
				ChooseStrength(a_trigger, a_payload, a_filling);
			} else {
				Make(bomb, recipe, amounts[i]);
			}
		};
		Show(box);
	}

	void ChooseStrength(int a_trigger, int a_payload, Part a_filling)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		const auto inv = Inventory(player);
		auto       box = std::make_shared<ListBox>();
		box->title = fmt::format("{} {} bomb with {}: how strong?", Bombs::kPayloadNames[a_payload], Bombs::kTriggerNames[a_trigger],
			NameOf(a_filling.form));
		for (int s = 0; s < Bombs::kStrengths; ++s) {
			const auto recipe = Recipe(a_trigger, a_filling, s);
			const int  can = CanMake(inv, recipe);
			box->entries.push_back(can > 0 ? fmt::format("{} (can make {}): {}", Bombs::kStrengthNames[s], can, Describe(recipe, nullptr)) :
			                                 fmt::format("{}: {}", Bombs::kStrengthNames[s], Describe(recipe, nullptr)));
		}
		box->back = true;
		box->onPick = [=](int i) {
			if (i < 0) {
				// back to the filling list if there was one to choose from, else to payloads
				const auto inv2 = Inventory(RE::PlayerCharacter::GetSingleton());
				const auto fills = Fillings(a_payload);
				const auto usable = std::ranges::count_if(fills, [&](const Part& f) { return Have(inv2, f.form) >= f.count; });
				if (usable > 1 || usable == 0) {
					ChooseFilling(a_trigger, a_payload);
				} else {
					ChoosePayload(a_trigger);
				}
			} else {
				ChooseAmount(a_trigger, a_payload, a_filling, i);
			}
		};
		Show(box);
	}

	void ChooseFilling(int a_trigger, int a_payload)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		const auto inv = Inventory(player);
		auto       fills = Fillings(a_payload);
		if (fills.empty()) {
			Notify(fmt::format("No {} fillings are set up (check [Fillings] in FirePots.ini).", Bombs::kPayloadNames[a_payload]));
			return;
		}
		UsableFirst(fills, [&](const Part& f) { return Have(inv, f.form) >= f.count; });
		const auto usable = std::ranges::count_if(fills, [&](const Part& f) { return Have(inv, f.form) >= f.count; });
		if (usable == 1) {
			ChooseStrength(a_trigger, a_payload, fills.front());  // nothing to choose
			return;
		}
		auto box = std::make_shared<ListBox>();
		box->title = fmt::format("{} payload: fill it with...", Bombs::kPayloadNames[a_payload]);
		for (const auto& f : fills) {
			const int have = Have(inv, f.form);
			box->entries.push_back(f.count == 1 ? fmt::format("{} ({})", NameOf(f.form), have) :
			                                      fmt::format("{} x{} ({})", NameOf(f.form), f.count, have));
		}
		box->back = true;
		box->onPick = [a_trigger, a_payload, fills](int i) {
			if (i < 0) {
				ChoosePayload(a_trigger);
			} else {
				ChooseStrength(a_trigger, a_payload, fills[i]);
			}
		};
		Show(box);
	}

	void ChoosePayload(int a_trigger)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		const auto inv = Inventory(player);
		auto       box = std::make_shared<ListBox>();
		box->title = fmt::format("{} bomb: what payload?", Bombs::kTriggerNames[a_trigger]);
		for (int p = 0; p < Bombs::kPayloads; ++p) {
			int best = 0;  // most mild bombs any one filling allows
			for (const auto& f : Fillings(p)) {
				best = (std::max)(best, CanMake(inv, Recipe(a_trigger, f, Bombs::kMild)));
			}
			box->entries.push_back(best > 0 ? fmt::format("{} (can make {})", Bombs::kPayloadNames[p], best) :
			                                  std::string(Bombs::kPayloadNames[p]));
		}
		box->back = true;
		box->onPick = [a_trigger](int i) {
			if (i < 0) {
				ChooseTrigger();
			} else {
				ChooseFilling(a_trigger, i);
			}
		};
		Show(box);
	}

	void ChooseTrigger()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		const auto inv = Inventory(player);
		auto       box = std::make_shared<ListBox>();
		box->title = fmt::format("Make a bomb: what kind? (empty containers: {})", ContainersHeld(inv));
		static constexpr const char* kLabels[Bombs::kTriggers] = { "Thrown, impact", "Thrown, short fuse", "Thrown, long fuse", "Tripwire (placed)" };
		for (int t = 0; t < Bombs::kTriggers; ++t) {
			box->entries.push_back(fmt::format("{}: {}", kLabels[t], Describe(TriggerParts(t), &inv)));
		}
		box->onPick = [](int i) {
			if (i >= 0) {
				ChoosePayload(i);
			}
		};
		Show(box);
	}
}

namespace Crafting
{
	void Open(RE::PlayerCharacter* a_player)
	{
		if (!Forms::Ready()) {
			Notify("FirePots.esp isn't loaded.");
			return;
		}
		if (!Config::craftInCombat && a_player->IsInCombat()) {
			Notify("You can't make bombs in combat.");
			return;
		}
		ChooseTrigger();
	}
}
