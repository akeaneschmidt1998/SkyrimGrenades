#include "LeveledLists.h"

#include "Config.h"
#include "Forms.h"

namespace
{
	// Adds one entry, keeping the list sorted by level (the engine expects that).
	bool AddEntry(RE::TESLevItem* a_list, RE::TESForm* a_form, std::uint16_t a_level, std::uint16_t a_count)
	{
		auto& entries = a_list->entries;
		const auto n = entries.size();
		if (n >= 255) {
			return false;  // numEntries is a byte
		}
		for (const auto& e : entries) {
			if (e.form == a_form && e.level == a_level) {
				return false;  // already there (e.g. a second Inject)
			}
		}
		entries.resize(n + 1);
		std::size_t at = n;
		while (at > 0 && entries[at - 1].level > a_level) {
			entries[at] = entries[at - 1];
			--at;
		}
		entries[at] = RE::LEVELED_OBJECT{};
		entries[at].form = a_form;
		entries[at].level = a_level;
		entries[at].count = a_count;
		entries[at].itemExtra = nullptr;
		a_list->numEntries = static_cast<std::uint8_t>(entries.size());
		return true;
	}
}

namespace LeveledLists
{
	void Inject()
	{
		if (!Config::addToLeveledLists) {
			logger::info("Leveled lists: off (bAddToLeveledLists = 0)");
			return;
		}
		const auto& types = Forms::Types();
		int         added = 0;
		for (const auto& loot : Config::loot) {
			if (loot.type < 0 || static_cast<std::size_t>(loot.type) >= types.size() || !types[loot.type].Ready()) {
				continue;
			}
			auto* form = Forms::Lookup(loot.list);
			auto* list = form ? form->As<RE::TESLevItem>() : nullptr;
			if (!list) {
				logger::warn("Leveled lists: {} is not a leveled item list", loot.list);
				continue;
			}
			if (AddEntry(list, types[loot.type].item, static_cast<std::uint16_t>(loot.level), static_cast<std::uint16_t>(loot.count))) {
				++added;
				logger::info("Leveled lists: {} x{} (level {}) -> {} [{:08X}]", types[loot.type].name, loot.count,
					loot.level, list->GetFormEditorID(), list->GetFormID());
			}
		}
		logger::info("Leveled lists: {} entries added", added);
	}
}
