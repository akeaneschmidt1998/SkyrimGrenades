#pragma once
// FirePots.esp forms, looked up once at kDataLoaded, plus the vanilla visuals copied onto them.

namespace Forms
{
	inline constexpr std::string_view kPlugin = "FirePots.esp";

	// Fixed local form IDs (tools/MakePlugin/Program.cs).
	inline constexpr RE::FormID kPotId = 0x800;
	inline constexpr RE::FormID kProjectileId = 0x801;
	inline constexpr RE::FormID kExplosionId = 0x802;

	inline RE::TESObjectMISC* pot = nullptr;
	inline RE::BGSProjectile* projectile = nullptr;
	inline RE::BGSExplosion*  explosion = nullptr;

	// False (and logged) if FirePots.esp isn't loaded or a form is missing.
	bool Load();
	[[nodiscard]] bool Ready();
}
