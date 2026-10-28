#pragma once
// FirePots.esp forms, looked up once at kDataLoaded, plus the vanilla visuals copied onto them.

namespace Forms
{
	inline constexpr std::string_view kPlugin = "FirePots.esp";

	// One throwable kind. Form IDs in the ESP are base, base+1 (projectile), base+2 (explosion)
	// (tools/MakePlugin/Program.cs).
	struct PotType
	{
		const char*        name;    // "Fire Pot"
		RE::FormID         baseId;  // local ID of the item
		const std::string* potModel;        // INI setting: model source for item + flying pot
		const std::string* explosionModel;  // INI setting: model source for the burst

		RE::TESObjectMISC* item = nullptr;
		RE::BGSProjectile* projectile = nullptr;
		RE::BGSExplosion*  explosion = nullptr;

		[[nodiscard]] bool Ready() const { return item && projectile && explosion; }
	};

	// Fire, Frost. Index order is the cycling order.
	std::vector<PotType>& Types();

	// "Skyrim.esm|0B9BD2" -> that form, or nullptr (logged).
	RE::TESForm* Lookup(const std::string& a_ref);

	// False (and logged) if FirePots.esp isn't loaded or a form is missing.
	bool Load();
	[[nodiscard]] bool Ready();
}
