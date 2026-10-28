#pragma once
// FirePots.esp forms, looked up once at kDataLoaded, plus the vanilla visuals copied onto them.

namespace Forms
{
	inline constexpr std::string_view kPlugin = "FirePots.esp";

	// One throwable kind (tools/MakePlugin/Program.cs): the item is a scroll at itemId; the
	// projectile and explosion are at baseId + 1 and + 2.
	struct PotType
	{
		const char*        name;    // "Fire Pot"
		RE::FormID         itemId;  // local ID of the scroll item
		RE::FormID         baseId;
		const std::string* potModel;        // INI setting: model source for item + flying pot
		const std::string* explosionModel;  // INI setting: model source for the burst
		RE::FormID         hazardId = 0;              // own lingering hazard (smoke cloud), 0 = none
		const std::string* hazardModel = nullptr;     // INI setting: model source for that hazard
		int                color = 0;                 // arc colour: 0 fire, 1 frost, 2 smoke, 3 thunder

		RE::ScrollItem*    item = nullptr;
		RE::BGSProjectile* projectile = nullptr;
		RE::BGSExplosion*  explosion = nullptr;

		[[nodiscard]] bool Ready() const { return item && projectile && explosion; }
	};

	// Fire, Frost, Smoke, Thunder. Index order is the cycling order.
	std::vector<PotType>& Types();

	// "Skyrim.esm|0B9BD2" -> that form, or nullptr (logged).
	RE::TESForm* Lookup(const std::string& a_ref);

	// False (and logged) if FirePots.esp isn't loaded or a form is missing.
	bool Load();
	[[nodiscard]] bool Ready();
}
