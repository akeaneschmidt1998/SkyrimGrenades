#pragma once
// Settings from Data/SKSE/Plugins/FirePots.ini, overridden by what you set in the MCM
// (Data/MCM/Settings/FirePots.ini, written by MCM Helper). Reloaded whenever the Journal menu
// (where the MCM lives) closes. Plain std only so the overlay TU can include it.
// Key codes (same as SkyUI MCM / SKSE): 1-255 = keyboard DirectX scan code,
// 256 = left mouse, 257 = right mouse, 258 = middle mouse, 259 = mouse 4, 260 = mouse 5. 0 = unbound.

#include "Bombs.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Config
{
	// --- Throw ---
	inline std::uint32_t throwKey = 34;          // G: hold to wind up, release to throw (or place)
	inline std::uint32_t cycleKey = 47;          // V: next bomb you carry
	inline std::uint32_t craftKey = 35;          // H: open the bomb-making menu
	inline bool          craftInCombat = true;   // allow making bombs while in combat
	inline std::uint32_t reloadConfigKey = 0;    // re-read the INI in game
	inline float         minSpeed = 250.0f;      // units/s at the shortest throw (~3.5 m/s)
	inline float         maxSpeed = 650.0f;      // units/s at full wind-up (~9 m/s, ~12 m at 45 degrees)
	inline float         chargeSeconds = 1.0f;   // time to full wind-up
	inline float         loftDegrees = 12.0f;    // extra upward angle over the crosshair
	inline float         placeReach = 250.0f;    // how far ahead a tripwire can be set down
	inline float         minHoldMs = 150.0f;     // shorter taps cancel (no bomb used)
	inline float         cooldownMs = 600.0f;    // between throws
	inline bool          cancelOnBlock = true;   // pressing block during a G wind-up cancels it
	inline std::string   throwNode = "NPC L Hand [LHnd]";       // where a left-hand / throw-key bomb leaves from
	inline std::string   throwNodeRight = "NPC R Hand [RHnd]";  // where a right-hand bomb leaves from
	inline float         projectileScale = 0.6f;  // size of the flying bomb model

	// --- Preview (off by default) ---
	inline bool  showArc = false;
	inline float arcStep = 1.0f / 30.0f;  // seconds per traced segment
	inline float arcMaxTime = 4.0f;       // seconds of flight traced at most
	inline bool  showBlastRing = true;
	inline float arcThickness = 3.0f;
	inline float arcOpacity = 0.9f;
	inline bool  showChargeMeter = true;  // wind-up meter (shown even with the arc off)

	// --- Calibration ---
	// 0 = start from 9.81 m/s^2 and replace it with the engine's real value measured on the first
	// throw. Set it to the logged "measured gravity" to skip the first-throw learning.
	inline float gravityUnits = 0.0f;
	inline bool  autoCalibrate = true;
	inline bool  calibrateSpeed = false;  // also correct the launch speed from the measurement

	// --- Visuals: borrowed from vanilla forms at load ("Plugin.esm|HEXID", or a mesh path) ---
	inline std::string triggerModel[Bombs::kTriggers]{
		"Skyrim.esm|0B9BD2",  // impact: glazed pot
		"Skyrim.esm|0B9BDE",  // short fuse: small clay jug
		"Skyrim.esm|0B9BD6",  // long fuse: large clay jug
		"Skyrim.esm|0F08F6",  // tripwire: Nordic pot
	};
	inline std::string explosionModel[Bombs::kPayloads]{
		"Skyrim.esm|088109",  // frag: FallingDustExplosion01
		"Skyrim.esm|070E5C",  // fire: TrapOilLampExplosion01
		"Skyrim.esm|044089",  // frost: ExplosionRuneFrost01
		"Skyrim.esm|01A13C",  // oil: FXdustDropSmExplosion (a splash)
		"Skyrim.esm|0D5100",  // smoke: FXDragonDustExplosion
		"Skyrim.esm|0D13E8",  // fear: ExplosionShockMass01 (flash)
		"Skyrim.esm|0665BB",  // frenzy: ExplosionRuneShock01
		"Skyrim.esm|0FDBBA",  // concussion: TG08BShockwaveExplosion
	};
	inline std::string smokeCloudModel = "Skyrim.esm|106380";      // DA13PustuleGasCloud
	inline std::string oilFireModel = "Skyrim.esm|109E74";         // TrapOilExplosionInitial

	// --- Crafting ---
	struct Ingredient
	{
		std::vector<std::string> forms;  // "Plugin.esm|HEXID"; more than one = any of them ("A or B")
		int                      count = 1;
		int                      group = 0;  // trigger (for trigger ingredients) or payload (for fillings)
	};
	inline std::vector<std::string> containers;         // any one (sContainer)
	inline std::vector<Ingredient>  triggerIngredients;  // every line needed; "or" = alternatives
	inline std::vector<Ingredient>  fillings;            // any one line per payload (sFragFilling, ...)

	// --- Bottle return: drinking wine gives the empty bottle back ---
	inline bool returnEmptyBottles = true;
	inline bool useBuiltInBottles = true;
	inline bool matchBottlesByModel = true;
	inline std::vector<std::pair<std::string, std::string>> bottles;  // sBottle = drink -> empty

	// --- Loot: bombs added to vanilla leveled lists at load ---
	struct Loot
	{
		std::string list;
		int         bomb = 0;  // Bombs index
		int         level = 1;
		int         count = 1;
	};
	inline bool              addToLeveledLists = true;
	inline std::vector<Loot> loot;  // sLoot = list, payload trigger strength, level, count

	// --- Messages ---
	inline bool printToConsole = true;
	inline bool showNotification = true;

	void Load();
}
