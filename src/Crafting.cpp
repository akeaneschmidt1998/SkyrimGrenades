#include "Crafting.h"

#include "Config.h"
#include "CraftShared.h"
#include "Forms.h"

#include <imgui.h>

namespace
{
	void Notify(const std::string& a_msg)
	{
		logger::info("{}", a_msg);
		if (Config::printToConsole) {
			if (auto* console = RE::ConsoleLog::GetSingleton()) {
				console->Print("[Fire Pots] %s", a_msg.c_str());
			}
		}
		if (Config::showNotification) {
			RE::SendHUDMessage::ShowHUDMessage(a_msg.c_str());
		}
	}

	bool HasName(const RE::TESForm* a_form)
	{
		const char* n = a_form ? a_form->GetName() : nullptr;
		return n && *n;
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

	// --- What the menu shows ----------------------------------------------------------------
	CraftModel::Model BuildModel(RE::PlayerCharacter* a_player)
	{
		CraftModel::Model m;
		const auto        inv = Inventory(a_player);
		auto note = [&](RE::TESBoundObject* a_obj) {
			const auto id = a_obj->GetFormID();
			m.items[id] = { a_obj->GetName(), Have(inv, a_obj) };
			return id;
		};
		for (auto* c : Containers()) {
			m.containers += Have(inv, c);
		}
		for (const auto& ing : Config::triggerIngredients) {
			if (ing.group < 0 || ing.group >= Bombs::kT) {
				continue;
			}
			CraftModel::Line line;
			for (const auto& ref : ing.forms) {
				if (auto* obj = Resolve(ref)) {
					line.alts.push_back({ note(obj), ing.count });
				}
			}
			if (!line.alts.empty()) {
				m.trigger[ing.group].push_back(std::move(line));
			}
		}
		for (const auto& ing : Config::fillings) {
			if (ing.group < 0 || ing.group >= Bombs::kP) {
				continue;
			}
			for (const auto& ref : ing.forms) {
				if (auto* obj = Resolve(ref)) {
					const auto id = note(obj);
					auto&      list = m.fillings[ing.group];
					if (std::ranges::none_of(list, [&](const CraftModel::Alt& a) { return a.id == id; })) {
						list.push_back({ id, ing.count });
					}
				}
			}
		}
		const auto& types = Forms::Types();
		for (int i = 0; i < Bombs::kCount && i < static_cast<int>(types.size()); ++i) {
			m.names[i] = types[i].name;
			m.carried[i] = types[i].item ? Have(inv, types[i].item) : 0;
		}
		return m;
	}

	void Publish(RE::PlayerCharacter* a_player)
	{
		auto m = BuildModel(a_player);
		std::scoped_lock l(CraftShared::lock);
		CraftShared::model = std::move(m);
	}

	// --- Crafting ---------------------------------------------------------------------------
	void Make(RE::PlayerCharacter* a_player, const CraftShared::Request& a_req)
	{
		const int bomb = Bombs::Index(a_req.trigger, a_req.payload, a_req.strength);
		auto&     type = Forms::Types()[bomb];
		if (!type.Ready()) {
			return;
		}
		// decide again from what's in the pack right now
		const auto model = BuildModel(a_player);
		const auto res = CraftModel::Resolve(model, a_req.trigger, a_req.payload, a_req.filling, a_req.strength);
		const int  n = (std::min)(a_req.qty, res.canMake);
		if (n <= 0) {
			Notify("You don't have what that takes.");
			return;
		}
		const auto inv = Inventory(a_player);
		int        left = n;
		for (auto* c : Containers()) {
			const int take = (std::min)(left, Have(inv, c));
			if (take > 0) {
				a_player->RemoveItem(c, take, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				left -= take;
			}
		}
		for (const auto& need : res.needs) {
			if (auto* obj = RE::TESForm::LookupByID<RE::TESBoundObject>(need.id)) {
				a_player->RemoveItem(obj, need.need * n, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			}
		}
		a_player->AddObjectToContainer(type.item, nullptr, n, nullptr);
		RE::PlaySound("UIAlchemyCreatePotion");
		Notify(n == 1 ? fmt::format("Made a {}", type.name) : fmt::format("Made {} x {}", n, type.name));
	}

	// --- Opening and closing: the game is paused and its controls are off while it's up ------
	std::uint32_t     g_savedEnabled = 0;
	std::uint32_t     g_savedStored = 0;
	std::atomic<bool> g_taskQueued{ false };

	void Close()
	{
		if (!CraftShared::open) {
			return;
		}
		CraftShared::open = false;
		if (auto* controls = RE::ControlMap::GetSingleton()) {
			controls->SetControlsState(g_savedEnabled, g_savedStored);
		}
		if (auto* ui = RE::UI::GetSingleton(); ui && ui->numPausesGame > 0) {
			--ui->numPausesGame;
		}
		logger::info("Bomb-making menu closed");
	}

	// Runs on the game's main thread (queued from the overlay's frame callback).
	void ProcessMenu()
	{
		g_taskQueued = false;
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !CraftShared::open) {
			return;
		}
		std::vector<CraftShared::Request> requests;
		{
			std::scoped_lock l(CraftShared::lock);
			requests.swap(CraftShared::requests);
		}
		for (const auto& r : requests) {
			Make(player, r);
		}
		if (!requests.empty()) {
			Publish(player);
		}
		if (CraftShared::closeRequested.exchange(false)) {
			Close();
		}
	}

	// Called by the overlay every frame while the menu is up (the game itself is paused).
	void OnFrame()
	{
		bool pending = CraftShared::closeRequested.load();
		{
			std::scoped_lock l(CraftShared::lock);
			pending = pending || !CraftShared::requests.empty();
		}
		if (pending && !g_taskQueued.exchange(true)) {
			SKSE::GetTaskInterface()->AddTask(ProcessMenu);
		}
	}

	// --- Input while it's up: everything goes to the menu --------------------------------------
	constexpr std::uint32_t kMouseOffset = 256;

	int KeyFromScanCode(std::uint32_t a_code)
	{
		switch (a_code) {
		case 200: case 17: return ImGuiKey_UpArrow;      // Up, W
		case 208: case 31: return ImGuiKey_DownArrow;    // Down, S
		case 203: case 30: return ImGuiKey_LeftArrow;    // Left, A
		case 205: case 32: return ImGuiKey_RightArrow;   // Right, D
		case 28: case 156: case 18: return ImGuiKey_Enter;  // Enter, keypad Enter, E (activate)
		case 57: return ImGuiKey_Space;
		case 1: return ImGuiKey_Escape;
		case 15: return ImGuiKey_Tab;
		case 14: return ImGuiKey_Backspace;
		case 42: case 54: return ImGuiKey_LeftShift;
		default: return ImGuiKey_None;
		}
	}

	int KeyFromGamepad(std::uint32_t a_code)
	{
		switch (a_code) {
		case 0x0001: return ImGuiKey_GamepadDpadUp;
		case 0x0002: return ImGuiKey_GamepadDpadDown;
		case 0x0004: return ImGuiKey_GamepadDpadLeft;
		case 0x0008: return ImGuiKey_GamepadDpadRight;
		case 0x1000: return ImGuiKey_GamepadFaceDown;   // A
		case 0x2000: return ImGuiKey_GamepadFaceRight;  // B
		case 0x4000: return ImGuiKey_GamepadFaceLeft;   // X
		case 0x8000: return ImGuiKey_GamepadFaceUp;     // Y
		case 0x0100: return ImGuiKey_GamepadL1;
		case 0x0200: return ImGuiKey_GamepadR1;
		default: return ImGuiKey_None;
		}
	}

	class MenuInput final : public RE::BSTEventSink<RE::InputEvent*>
	{
	public:
		static MenuInput* GetSingleton()
		{
			static MenuInput sink;
			return &sink;
		}

		RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override
		{
			if (!a_event || !CraftShared::open) {
				return RE::BSEventNotifyControl::kContinue;
			}
			std::vector<CraftShared::InputEvent> out;
			using K = CraftShared::InputEvent::Kind;
			for (auto* e = *a_event; e; e = e->next) {
				if (e->GetEventType() == RE::INPUT_EVENT_TYPE::kMouseMove) {
					auto* m = static_cast<RE::MouseMoveEvent*>(e);
					out.push_back({ K::kMouseDelta, static_cast<float>(m->mouseInputX), static_cast<float>(m->mouseInputY) });
					continue;
				}
				auto* b = e->GetEventType() == RE::INPUT_EVENT_TYPE::kButton ? e->AsButtonEvent() : nullptr;
				if (!b || !(b->IsDown() || b->IsUp())) {
					continue;
				}
				const bool down = b->IsDown();
				const auto code = b->GetIDCode();
				switch (b->GetDevice()) {
				case RE::INPUT_DEVICE::kMouse:
					if (code <= 2) {
						out.push_back({ K::kMouseButton, 0.0f, 0.0f, static_cast<int>(code), down });
					} else if (down && (code == 8 || code == 9)) {
						out.push_back({ K::kWheel, code == 8 ? 1.0f : -1.0f });
					}
					break;
				case RE::INPUT_DEVICE::kKeyboard:
					if (down && code == Config::craftKey && Config::craftKey < kMouseOffset) {
						CraftShared::closeRequested = true;  // the craft key closes it again
					} else if (const int k = KeyFromScanCode(code); k != ImGuiKey_None) {
						out.push_back({ K::kKey, 0.0f, 0.0f, k, down });
					}
					break;
				case RE::INPUT_DEVICE::kGamepad:
					if (const int k = KeyFromGamepad(code); k != ImGuiKey_None) {
						out.push_back({ K::kKey, 0.0f, 0.0f, k, down });
					}
					break;
				default:
					break;
				}
			}
			if (!out.empty()) {
				std::scoped_lock l(CraftShared::lock);
				CraftShared::events.insert(CraftShared::events.end(), out.begin(), out.end());
			}
			return RE::BSEventNotifyControl::kContinue;
		}

	private:
		MenuInput() = default;
	};
}

namespace Crafting
{
	void Register()
	{
		CraftShared::onFrame = OnFrame;
		if (auto* devices = RE::BSInputDeviceManager::GetSingleton()) {
			devices->AddEventSink(MenuInput::GetSingleton());
		}
	}

	bool IsOpen() { return CraftShared::open; }

	void Open(RE::PlayerCharacter* a_player)
	{
		if (CraftShared::open) {
			return;
		}
		if (!Forms::Ready()) {
			Notify("FirePots.esp isn't loaded.");
			return;
		}
		if (!Config::craftInCombat && a_player->IsInCombat()) {
			Notify("You can't make bombs in combat.");
			return;
		}
		Publish(a_player);
		{
			std::scoped_lock l(CraftShared::lock);
			CraftShared::events.clear();
			CraftShared::requests.clear();
		}
		CraftShared::closeRequested = false;
		CraftShared::justOpened = true;
		// pause the game and turn its controls off (restored exactly on close)
		if (auto* controls = RE::ControlMap::GetSingleton()) {
			controls->GetControlsState(g_savedEnabled, g_savedStored);
			controls->ToggleControls(RE::ControlMap::UEFlag::kAll, false, false);
		}
		if (auto* ui = RE::UI::GetSingleton()) {
			++ui->numPausesGame;
		}
		CraftShared::open = true;
		logger::info("Bomb-making menu opened");
	}

	void ForceClose() { Close(); }
}
