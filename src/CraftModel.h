#pragma once
// What the bomb-making menu shows: every ingredient involved, how many you carry, and the rules
// for what a bomb takes. Built on the game thread (Crafting.cpp), read by the menu drawing
// (CraftUI.cpp, render thread) and by the offline preview. Plain std only.

#include "Bombs.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace CraftModel
{
	struct Item
	{
		std::string name;
		int         have = 0;
	};

	struct Alt  // one way to satisfy a line: this item, this many (per mild bomb)
	{
		std::uint32_t id = 0;
		int           count = 1;
	};

	struct Line  // a trigger needs every line, each from one of its alternatives
	{
		std::vector<Alt> alts;
	};

	struct Model
	{
		std::map<std::uint32_t, Item> items;      // every form a recipe mentions (by runtime form ID)
		int                           containers = 0;  // empty containers carried
		std::vector<Line>             trigger[Bombs::kT];
		std::vector<Alt>              fillings[Bombs::kP];  // any one per payload
		int                           carried[Bombs::kCount]{};  // bombs you have
		std::string                   names[Bombs::kCount];       // the items' in-game names

		int Have(std::uint32_t a_id) const
		{
			const auto it = items.find(a_id);
			return it == items.end() ? 0 : it->second.have;
		}
		std::string Name(std::uint32_t a_id) const
		{
			const auto it = items.find(a_id);
			return it == items.end() ? "?" : it->second.name;
		}
	};

	struct Need
	{
		std::uint32_t id = 0;
		std::string   name;
		int           need = 0;  // per bomb at this strength
		int           have = 0;
		int           alternatives = 0;  // other items that would do instead (for the menu text)
	};

	struct Resolved
	{
		std::vector<Need> needs;
		int               canMake = 0;
	};

	// What one bomb of (trigger, payload, filling choice, strength) takes and how many you could
	// make. A line with alternatives uses the first one you carry enough of (else the first).
	// The same item from the trigger and the filling (Fire Salts twice) is merged.
	inline Resolved Resolve(const Model& a_m, int a_trigger, int a_payload, int a_filling, int a_strength)
	{
		Resolved r;
		const int mult = a_strength + 1;
		auto add = [&](std::uint32_t a_id, int a_count, int a_alts) {
			for (auto& n : r.needs) {
				if (n.id == a_id) {
					n.need += a_count;
					return;
				}
			}
			r.needs.push_back({ a_id, a_m.Name(a_id), a_count, a_m.Have(a_id), a_alts });
		};
		for (const auto& line : a_m.trigger[a_trigger]) {
			if (line.alts.empty()) {
				continue;
			}
			const Alt* pick = &line.alts.front();
			for (const auto& a : line.alts) {
				if (a_m.Have(a.id) >= a.count * mult) {
					pick = &a;
					break;
				}
			}
			add(pick->id, pick->count * mult, static_cast<int>(line.alts.size()) - 1);
		}
		const auto& fills = a_m.fillings[a_payload];
		if (a_filling < 0 || a_filling >= static_cast<int>(fills.size())) {
			return r;  // nothing to fill it with: can't make any
		}
		add(fills[a_filling].id, fills[a_filling].count * mult, 0);
		r.canMake = a_m.containers;
		for (const auto& n : r.needs) {
			r.canMake = (std::min)(r.canMake, n.need > 0 ? n.have / n.need : 0);
		}
		r.canMake = (std::max)(r.canMake, 0);
		return r;
	}

	// The filling of a payload you could make the most bombs with (or 0).
	inline int BestFilling(const Model& a_m, int a_trigger, int a_payload, int a_strength)
	{
		int best = 0, bestCount = -1;
		for (int f = 0; f < static_cast<int>(a_m.fillings[a_payload].size()); ++f) {
			const int c = Resolve(a_m, a_trigger, a_payload, f, a_strength).canMake;
			if (c > bestCount) {
				best = f;
				bestCount = c;
			}
		}
		return best;
	}
}
