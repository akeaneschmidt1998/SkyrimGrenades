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

	// Containers that look the same in game (four "Empty Wine Bottle" forms) are one choice.
	struct ContainerGroup
	{
		std::string                      name;
		std::vector<RE::TESBoundObject*> forms;

		int Have(const Inv& a_inv) const
		{
			int n = 0;
			for (auto* f : forms) {
				n += ::Have(a_inv, f);
			}
			return n;
		}
	};

	struct Filling
	{
		RE::TESBoundObject* form = nullptr;
		int                 per = 1;  // per pot
		int                 type = 0;
	};

	std::vector<ContainerGroup> AllContainers()
	{
		std::vector<ContainerGroup> out;
		for (const auto& ref : Config::containers) {
			auto* form = Forms::Lookup(ref);
			auto* obj = form ? form->As<RE::TESBoundObject>() : nullptr;
			if (!obj || !HasName(obj)) {
				continue;  // nameless items can't be picked up, so never carried
			}
			const auto name = NameOf(obj);
			auto       it = std::ranges::find(out, name, &ContainerGroup::name);
			if (it == out.end()) {
				out.push_back({ name, { obj } });
			} else if (std::ranges::find(it->forms, obj) == it->forms.end()) {
				it->forms.push_back(obj);
			}
		}
		return out;
	}

	std::vector<Filling> AllFillings(int a_type)
	{
		std::vector<Filling> out;
		const auto&          types = Forms::Types();
		for (const auto& f : Config::fillings) {
			if (f.type != a_type || static_cast<std::size_t>(f.type) >= types.size() || !types[f.type].Ready()) {
				continue;
			}
			auto* form = Forms::Lookup(f.form);
			if (auto* obj = form ? form->As<RE::TESBoundObject>() : nullptr; obj && HasName(obj)) {
				out.push_back({ obj, f.count, f.type });
			}
		}
		return out;
	}

	std::vector<int> ReadyTypes()
	{
		std::vector<int> out;
		const auto&      types = Forms::Types();
		for (std::size_t i = 0; i < types.size(); ++i) {
			if (types[i].Ready()) {
				out.push_back(static_cast<int>(i));
			}
		}
		return out;
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

	// --- The three steps ------------------------------------------------------------------
	void ChooseType();
	void ChooseContainer(int a_type);
	void ChooseFilling(int a_type, const ContainerGroup& a_container);
	void ChooseAmount(const ContainerGroup& a_container, const Filling& a_filling);

	void Make(const ContainerGroup& a_container, const Filling& a_filling, int a_wanted)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto& type = Forms::Types()[a_filling.type];
		if (!player || !type.Ready()) {
			return;
		}
		// count again: the inventory may have changed while the menu was up
		const auto inv = Inventory(player);
		const int  n = (std::min)({ a_wanted, a_container.Have(inv), Have(inv, a_filling.form) / a_filling.per });
		if (n <= 0) {
			Notify("You no longer have what that takes.");
			return;
		}
		int left = n;
		for (auto* f : a_container.forms) {
			const int take = (std::min)(left, Have(inv, f));
			if (take > 0) {
				player->RemoveItem(f, take, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				left -= take;
			}
		}
		player->RemoveItem(a_filling.form, n * a_filling.per, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
		player->AddObjectToContainer(type.item, nullptr, n, nullptr);
		RE::PlaySound("UIAlchemyCreatePotion");
		Notify(n == 1 ? fmt::format("Made a {}", type.name) : fmt::format("Made {} {}s", n, type.name));
	}

	void ChooseAmount(const ContainerGroup& a_container, const Filling& a_filling)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		const auto  inv = Inventory(player);
		const auto& potName = Forms::Types()[a_filling.type].name;
		const int   haveC = a_container.Have(inv);
		const int   haveF = Have(inv, a_filling.form);
		const int   max = (std::min)(haveC, haveF / a_filling.per);

		auto box = std::make_shared<ListBox>();
		box->back = true;
		std::vector<int> amounts;
		if (max <= 0) {
			// just information: what it takes and what's missing
			box->title = fmt::format("A {} takes 1 {} (you have {}) and {} {} (you have {}).", potName,
				a_container.name, haveC, a_filling.per, NameOf(a_filling.form), haveF);
		} else if (max == 1) {
			Make(a_container, a_filling, 1);
			return;
		} else {
			amounts.push_back(1);
			if (max > 5) {
				amounts.push_back(5);
			}
			amounts.push_back(max);
			box->title = fmt::format("How many {}s? (up to {})", potName, max);
			for (const int a : amounts) {
				box->entries.push_back(a == max ? fmt::format("All ({})", a) : std::to_string(a));
			}
		}
		box->onPick = [a_container, a_filling, amounts](int i) {
			if (i < 0) {
				ChooseFilling(a_filling.type, a_container);
			} else {
				Make(a_container, a_filling, amounts[i]);
			}
		};
		Show(box);
	}

	void ChooseFilling(int a_type, const ContainerGroup& a_container)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		const auto inv = Inventory(player);
		auto       fills = AllFillings(a_type);
		UsableFirst(fills, [&](const Filling& f) { return Have(inv, f.form) >= f.per; });

		auto box = std::make_shared<ListBox>();
		const int haveC = a_container.Have(inv);
		const auto& pot = Forms::Types()[a_type].name;
		box->title = haveC > 0 ? fmt::format("{}: fill the {} with...", pot, a_container.name) :
		                         fmt::format("{}: fill the {} with... (you have none)", pot, a_container.name);
		for (const auto& f : fills) {
			const int  have = Have(inv, f.form);
			const auto qty = f.per == 1 ? fmt::format("({})", have) : fmt::format("x{} ({})", f.per, have);
			box->entries.push_back(fmt::format("{} {}", NameOf(f.form), qty));
		}
		box->back = true;
		box->onPick = [a_type, a_container, fills](int i) {
			if (i < 0) {
				ChooseContainer(a_type);
			} else {
				ChooseAmount(a_container, fills[i]);
			}
		};
		Show(box);
	}

	void ChooseContainer(int a_type)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		const auto inv = Inventory(player);
		auto       containers = AllContainers();
		if (containers.empty()) {
			Notify("No containers are set up (check [Containers] in FirePots.ini).");
			return;
		}
		UsableFirst(containers, [&](const ContainerGroup& c) { return c.Have(inv) > 0; });

		auto box = std::make_shared<ListBox>();
		box->title = fmt::format("Make a {}: choose a container", Forms::Types()[a_type].name);
		for (const auto& c : containers) {
			box->entries.push_back(fmt::format("{} ({})", c.name, c.Have(inv)));
		}
		box->back = ReadyTypes().size() > 1;
		box->onPick = [a_type, containers](int i) {
			if (i < 0) {
				ChooseType();
			} else {
				ChooseFilling(a_type, containers[i]);
			}
		};
		Show(box);
	}

	// Step 0: which pot. Each shows how many you could make right now.
	void ChooseType()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		const auto types = ReadyTypes();
		if (types.size() == 1) {
			ChooseContainer(types.front());
			return;
		}
		const auto inv = Inventory(player);
		int        containers = 0;
		for (const auto& c : AllContainers()) {
			containers += c.Have(inv);
		}
		auto box = std::make_shared<ListBox>();
		box->title = "Make a pot: which kind?";
		for (const int t : types) {
			int fills = 0;  // pots' worth of filling
			for (const auto& f : AllFillings(t)) {
				fills += Have(inv, f.form) / f.per;
			}
			const int can = (std::min)(containers, fills);
			box->entries.push_back(can > 0 ? fmt::format("{} (can make {})", Forms::Types()[t].name, can) :
			                                 fmt::format("{}", Forms::Types()[t].name));
		}
		box->onPick = [types](int i) {
			if (i >= 0) {
				ChooseContainer(types[i]);
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
			Notify("You can't make pots in combat.");
			return;
		}
		ChooseType();
	}
}
