#include "Crafting.h"

#include "Config.h"
#include "Forms.h"

namespace
{
	constexpr std::size_t  kPageSize = 8;     // entries per message box before "More..."
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

	std::string NameOf(const RE::TESForm* a_form)
	{
		const char* n = a_form ? a_form->GetName() : nullptr;
		return n && *n ? n : fmt::format("{:08X}", a_form ? a_form->GetFormID() : 0u);
	}

	std::map<RE::TESBoundObject*, int> Inventory(RE::PlayerCharacter* a_player)
	{
		std::map<RE::TESBoundObject*, int> out;
		for (const auto& [obj, data] : a_player->GetInventory()) {
			if (obj && data.first > 0) {
				out[obj] += data.first;
			}
		}
		return out;
	}

	struct Container
	{
		RE::TESBoundObject* form = nullptr;
		int                 have = 0;
	};

	struct Filling
	{
		RE::TESBoundObject* form = nullptr;
		int                 have = 0;
		int                 per = 1;  // per pot
		int                 type = 0;
	};

	std::vector<Container> CarriedContainers(const std::map<RE::TESBoundObject*, int>& a_inv)
	{
		std::vector<Container> out;
		for (const auto& ref : Config::containers) {
			auto* form = Forms::Lookup(ref);
			auto* obj = form ? form->As<RE::TESBoundObject>() : nullptr;
			const auto it = obj ? a_inv.find(obj) : a_inv.end();
			const bool dup = std::ranges::any_of(out, [&](const Container& c) { return c.form == obj; });
			if (it != a_inv.end() && it->second > 0 && !dup) {
				out.push_back({ obj, it->second });
			}
		}
		return out;
	}

	std::vector<Filling> CarriedFillings(const std::map<RE::TESBoundObject*, int>& a_inv)
	{
		std::vector<Filling> out;
		const auto& types = Forms::Types();
		for (const auto& f : Config::fillings) {
			if (f.type < 0 || static_cast<std::size_t>(f.type) >= types.size() || !types[f.type].Ready()) {
				continue;
			}
			auto* form = Forms::Lookup(f.form);
			auto* obj = form ? form->As<RE::TESBoundObject>() : nullptr;
			const auto it = obj ? a_inv.find(obj) : a_inv.end();
			if (it != a_inv.end() && it->second >= f.count) {
				out.push_back({ obj, it->second, f.count, f.type });
			}
		}
		return out;
	}

	// --- A message box with a list of choices --------------------------------------------
	// a_onPick gets the chosen entry's index into a_entries, or -1 for Back (only if a_back).
	struct ListBox
	{
		std::string                        title;
		std::vector<std::string>           entries;
		bool                               back = false;
		std::function<void(int)>           onPick;
		std::size_t                        page = 0;
	};

	void Show(std::shared_ptr<ListBox> a_box);

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
		const bool        more = a_box->entries.size() > last || a_box->page > 0;

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
	void ChooseContainer();
	void ChooseFilling(RE::TESBoundObject* a_container);
	void ChooseAmount(RE::TESBoundObject* a_container, RE::TESBoundObject* a_filling, int a_per, int a_type);

	void Make(RE::TESBoundObject* a_container, RE::TESBoundObject* a_filling, int a_per, int a_type, int a_wanted)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto& type = Forms::Types()[a_type];
		if (!player || !type.Ready()) {
			return;
		}
		// count again: the inventory may have changed while the menu was up
		const auto inv = Inventory(player);
		const auto get = [&](RE::TESBoundObject* o) { const auto it = inv.find(o); return it == inv.end() ? 0 : it->second; };
		const int  n = (std::min)({ a_wanted, get(a_container), get(a_filling) / a_per });
		if (n <= 0) {
			Notify("You no longer have what that takes.");
			return;
		}
		player->RemoveItem(a_container, n, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
		player->RemoveItem(a_filling, n * a_per, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
		player->AddObjectToContainer(type.item, nullptr, n, nullptr);
		RE::PlaySound("UIAlchemyCreatePotion");
		Notify(n == 1 ? fmt::format("Made a {}", type.name) : fmt::format("Made {} {}s", n, type.name));
	}

	void ChooseAmount(RE::TESBoundObject* a_container, RE::TESBoundObject* a_filling, int a_per, int a_type)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		const auto inv = Inventory(player);
		const auto get = [&](RE::TESBoundObject* o) { const auto it = inv.find(o); return it == inv.end() ? 0 : it->second; };
		const int  max = (std::min)(get(a_container), get(a_filling) / a_per);
		if (max <= 1) {
			Make(a_container, a_filling, a_per, a_type, 1);
			return;
		}
		std::vector<int> amounts{ 1 };
		if (max > 5) {
			amounts.push_back(5);
		}
		amounts.push_back(max);

		auto box = std::make_shared<ListBox>();
		box->title = fmt::format("How many {}s? (up to {})", Forms::Types()[a_type].name, max);
		for (const int a : amounts) {
			box->entries.push_back(a == max ? fmt::format("All ({})", a) : std::to_string(a));
		}
		box->back = true;
		box->onPick = [=](int i) {
			if (i < 0) {
				ChooseFilling(a_container);
			} else {
				Make(a_container, a_filling, a_per, a_type, amounts[i]);
			}
		};
		Show(box);
	}

	void ChooseFilling(RE::TESBoundObject* a_container)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		const auto fills = CarriedFillings(Inventory(player));
		if (fills.empty()) {
			Notify("You have nothing to fill it with.");
			return;
		}
		auto box = std::make_shared<ListBox>();
		box->title = fmt::format("Fill the {} with...", NameOf(a_container));
		for (const auto& f : fills) {
			const auto& pot = Forms::Types()[f.type].name;
			box->entries.push_back(f.per == 1 ? fmt::format("{} ({}) - {}", NameOf(f.form), f.have, pot) :
			                                    fmt::format("{} x{} ({}) - {}", NameOf(f.form), f.per, f.have, pot));
		}
		box->back = true;
		box->onPick = [a_container, fills](int i) {
			if (i < 0) {
				ChooseContainer();
			} else {
				ChooseAmount(a_container, fills[i].form, fills[i].per, fills[i].type);
			}
		};
		Show(box);
	}

	void ChooseContainer()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		const auto inv = Inventory(player);
		const auto containers = CarriedContainers(inv);
		if (containers.empty()) {
			Notify("You need an empty bottle, jug or pot to make a fire pot.");
			return;
		}
		if (CarriedFillings(inv).empty()) {
			Notify("You have nothing to fill a pot with (Fire Salts, Dwarven Oil, Troll Fat, Frost Salts...).");
			return;
		}
		auto box = std::make_shared<ListBox>();
		box->title = "Make a pot: choose a container";
		for (const auto& c : containers) {
			box->entries.push_back(fmt::format("{} ({})", NameOf(c.form), c.have));
		}
		box->onPick = [containers](int i) {
			if (i >= 0) {
				ChooseFilling(containers[i].form);
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
		ChooseContainer();
	}
}
