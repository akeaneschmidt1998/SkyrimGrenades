#pragma once
// FirePots.esp forms, looked up once at kDataLoaded, plus the vanilla visuals copied onto them.

#include "Bombs.h"

namespace Forms
{
	inline constexpr std::string_view kPlugin = "FirePots.esp";

	// One bomb (trigger x payload x strength), Bombs::Index order.
	struct PotType
	{
		int                index = 0;
		int                trigger = 0;
		int                payload = 0;
		int                strength = 0;
		std::string        name;  // the item's in-game name
		bool               debug = false;  // a debug smoke bomb (after the real ones)

		RE::ScrollItem*    item = nullptr;
		RE::BGSProjectile* projectile = nullptr;
		RE::BGSExplosion*  explosion = nullptr;

		[[nodiscard]] bool Ready() const { return item && projectile && explosion; }
	};

	// Every bomb, Bombs::Index order, then the debug smoke bombs (index Bombs::kCount + k).
	std::vector<PotType>& Types();
	[[nodiscard]] int DebugIndex(int k);

	// Sets each bomb's price from what its ingredients are worth (FirePots.ini fValueMult).
	void SetPrices();

	inline RE::BGSProjectile* oilFireProjectile = nullptr;

	// "Skyrim.esm|0B9BD2" -> that form, or nullptr (logged).
	RE::TESForm* Lookup(const std::string& a_ref);

	// False (and logged) if FirePots.esp isn't loaded or a form is missing.
	bool Load();
	[[nodiscard]] bool Ready();
}
