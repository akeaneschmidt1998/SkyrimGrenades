#include "BottleReturn.h"

#include "Config.h"
#include "Forms.h"

namespace
{
	std::unordered_map<RE::FormID, RE::TESBoundObject*> g_table;  // drink form ID -> empty container

	std::string Lower(std::string s)
	{
		std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return s;
	}

	// "Clutter\Wine\WineBottle01AEmpty.nif" -> "clutter\wine\winebottle01a.nif"
	std::string ModelKey(const char* a_model)
	{
		std::string m = Lower(a_model ? a_model : "");
		std::ranges::replace(m, '/', '\\');
		if (const auto p = m.find("empty"); p != std::string::npos) {
			m.erase(p, 5);
		}
		while (m.find("\\\\") != std::string::npos) {
			m.erase(m.find("\\\\"), 1);
		}
		return m;
	}

	void Add(RE::AlchemyItem* a_drink, RE::TESBoundObject* a_empty, const char* a_how)
	{
		if (!a_drink || !a_empty || g_table.contains(a_drink->GetFormID())) {
			return;
		}
		g_table[a_drink->GetFormID()] = a_empty;
		logger::info("Bottle return: {} [{:08X}] -> {} [{:08X}] ({})", a_drink->GetName(), a_drink->GetFormID(),
			a_empty->GetName(), a_empty->GetFormID(), a_how);
	}

	void AddPair(const std::string& a_drink, const std::string& a_empty, const char* a_how)
	{
		auto* d = Forms::Lookup(a_drink);
		auto* e = Forms::Lookup(a_empty);
		Add(d ? d->As<RE::AlchemyItem>() : nullptr, e ? e->As<RE::TESBoundObject>() : nullptr, a_how);
	}

	void Build()
	{
		g_table.clear();
		for (const auto& [drink, empty] : Config::bottles) {
			AddPair(drink, empty, "FirePots.ini");
		}
		if (Config::useBuiltInBottles) {
			// clang-format off
			static const std::pair<const char*, const char*> builtIn[] = {
				{ "Skyrim.esm|03133C", "Skyrim.esm|0F2015" },      // Wine -> Empty Wine Bottle (02B)
				{ "Skyrim.esm|0C5348", "Skyrim.esm|0F2014" },      // Wine -> (02A)
				{ "Skyrim.esm|03133B", "Skyrim.esm|0F2012" },      // Alto Wine -> (01A)
				{ "Skyrim.esm|0C5349", "Skyrim.esm|0F2012" },      // Alto Wine -> (01A)
				{ "Skyrim.esm|0F257E", "Skyrim.esm|0F2012" },      // Alto Wine (quest copy) -> (01A)
				{ "Skyrim.esm|085368", "Skyrim.esm|0FED17" },      // Spiced Wine -> empty spiced wine bottle
				{ "Skyrim.esm|01895F", "Skyrim.esm|0F2013" },      // Firebrand Wine -> (01B)
				{ "HearthFires.esm|003536", "Dawnguard.esm|0093F5" },  // HearthFire wine bottles
				{ "HearthFires.esm|003535", "Dawnguard.esm|0093F6" },
				{ "Dragonborn.esm|0320DF", "Skyrim.esm|0F2013" },  // Emberbrand Wine
			};
			// clang-format on
			for (const auto& [drink, empty] : builtIn) {
				AddPair(drink, empty, "built-in");
			}
		}
		if (Config::matchBottlesByModel) {
			// empty wine bottles, keyed by their model without the word "Empty"
			std::unordered_map<std::string, RE::TESBoundObject*> empties;
			for (const auto& ref : Config::containers) {
				auto* form = Forms::Lookup(ref);
				auto* obj = form ? form->As<RE::TESBoundObject>() : nullptr;
				auto* model = form ? skyrim_cast<RE::TESModel*>(form) : nullptr;
				const char* path = model ? model->GetModel() : nullptr;
				const auto lower = Lower(path ? path : "");
				if (obj && lower.find("empty") != std::string::npos && lower.find("wine") != std::string::npos) {
					empties.emplace(ModelKey(path), obj);
				}
			}
			if (auto* dh = RE::TESDataHandler::GetSingleton(); dh && !empties.empty()) {
				for (auto* drink : dh->GetFormArray<RE::AlchemyItem>()) {
					if (!drink || drink->IsPoison() || drink->IsMedicine()) {
						continue;
					}
					const auto it = empties.find(ModelKey(drink->GetModel()));
					if (it != empties.end()) {
						Add(drink, it->second, "same bottle model");
					}
				}
			}
		}
		// Everything else that's a wine or a brandy (any mod's too): matched by name or by a model in
		// the wine folder; the bottle that looks most like it, else a plain empty wine bottle.
		if (auto* dh = RE::TESDataHandler::GetSingleton()) {
			auto* plain = Forms::Lookup("Skyrim.esm|0F2012");     // WineBottle01AEmpty
			auto* spiced = Forms::Lookup("Skyrim.esm|0FED17");    // WineSolitudeSpicedBottleEmpty
			auto* b01b = Forms::Lookup("Skyrim.esm|0F2013");
			auto* b02a = Forms::Lookup("Skyrim.esm|0F2014");
			auto* b02b = Forms::Lookup("Skyrim.esm|0F2015");
			for (auto* drink : dh->GetFormArray<RE::AlchemyItem>()) {
				if (!drink || drink->IsPoison() || drink->IsMedicine() || g_table.contains(drink->GetFormID())) {
					continue;
				}
				const auto name = Lower(drink->GetName() ? drink->GetName() : "");
				const auto model = Lower(drink->GetModel() ? drink->GetModel() : "");
				const bool wine = name.find("wine") != std::string::npos || name.find("brandy") != std::string::npos ||
				                  model.find("\\wine\\") != std::string::npos || model.find("winebottle") != std::string::npos ||
				                  model.find("brandy") != std::string::npos;
				if (!wine) {
					continue;
				}
				RE::TESForm* empty = plain;
				if (name.find("spiced") != std::string::npos || model.find("spiced") != std::string::npos) {
					empty = spiced;
				} else if (model.find("winebottle01b") != std::string::npos) {
					empty = b01b;
				} else if (model.find("winebottle02a") != std::string::npos) {
					empty = b02a;
				} else if (model.find("winebottle02") != std::string::npos) {
					empty = b02b;
				}
				Add(drink, empty ? empty->As<RE::TESBoundObject>() : nullptr, "a wine or brandy");
			}
		}
		logger::info("Bottle return: {} drinks give an empty bottle back", g_table.size());
	}

	class EquipSink final : public RE::BSTEventSink<RE::TESEquipEvent>
	{
	public:
		static EquipSink* GetSingleton()
		{
			static EquipSink sink;
			return &sink;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* a_event, RE::BSTEventSource<RE::TESEquipEvent>*) override
		{
			// Drinking a potion/food item fires an equip event for it.
			if (!a_event || !a_event->equipped || !Config::returnEmptyBottles || !a_event->actor ||
				!a_event->actor->IsPlayerRef()) {
				return RE::BSEventNotifyControl::kContinue;
			}
			const auto it = g_table.find(a_event->baseObject);
			if (it == g_table.end()) {
				return RE::BSEventNotifyControl::kContinue;
			}
			auto* empty = it->second;
			SKSE::GetTaskInterface()->AddTask([empty]() {
				if (auto* player = RE::PlayerCharacter::GetSingleton()) {
					player->AddObjectToContainer(empty, nullptr, 1, nullptr);
				}
			});
			return RE::BSEventNotifyControl::kContinue;
		}

	private:
		EquipSink() = default;
	};
}

namespace BottleReturn
{
	void Init()
	{
		Build();
		if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
			events->AddEventSink<RE::TESEquipEvent>(EquipSink::GetSingleton());
			logger::info("Bottle return: {}", Config::returnEmptyBottles ? "on" : "off (bReturnEmptyBottles = 0)");
		}
	}
}
