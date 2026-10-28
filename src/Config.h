#pragma once
// Settings from Data/SKSE/Plugins/FirePots.ini. Plain std only so the overlay TU can include it.
// Key codes (same as SkyUI MCM / SKSE): 1-255 = keyboard DirectX scan code,
// 256 = left mouse, 257 = right mouse, 258 = middle mouse, 259 = mouse 4, 260 = mouse 5. 0 = unbound.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Config
{
	// --- Throw ---
	inline std::uint32_t throwKey = 34;       // G: hold to wind up, release to throw
	inline std::uint32_t cycleKey = 47;       // V: switch pot type (fire / frost)
	inline std::uint32_t craftKey = 35;       // H: open the pot-making menu
	inline bool          craftInCombat = true;  // allow making pots while in combat
	inline std::uint32_t reloadConfigKey = 0;  // re-read the INI in game
	inline float         minSpeed = 700.0f;    // units/s at the shortest throw
	inline float         maxSpeed = 1700.0f;   // units/s at full wind-up
	inline float         chargeSeconds = 1.0f;  // time to full wind-up
	inline float         loftDegrees = 12.0f;   // extra upward angle over the crosshair
	inline float         minHoldMs = 150.0f;    // shorter taps cancel (no pot used)
	inline float         cooldownMs = 600.0f;   // between throws
	inline bool          cancelOnBlock = true;  // pressing block during the wind-up cancels it
	inline std::string   throwNode = "NPC L Hand [LHnd]";  // where the pot leaves from
	inline float         projectileScale = 0.6f;  // size of the flying pot model

	// --- Preview ---
	inline bool  showArc = true;
	inline float arcStep = 1.0f / 30.0f;  // seconds per traced segment
	inline float arcMaxTime = 4.0f;       // seconds of flight traced at most
	inline float blastRadius = 160.0f;    // ring drawn at the landing point (match the ESP)
	inline bool  showBlastRing = true;
	inline float arcThickness = 3.0f;
	inline float arcOpacity = 0.9f;
	inline bool  showChargeMeter = true;

	// --- Calibration ---
	// 0 = start from 9.81 m/s^2 and replace it with the engine's real value measured on the first
	// throw. Set it to the logged "measured gravity" to skip the first-throw learning.
	inline float gravityUnits = 0.0f;
	inline bool  autoCalibrate = true;

	// --- Visuals: borrowed from vanilla forms at load ("Plugin.esm|HEXID", or a mesh path) ---
	inline std::string firePotModel = "Skyrim.esm|0B9BD2";         // GlazedPot01: item + flying pot
	inline std::string fireExplosionModel = "Skyrim.esm|070E5C";   // TrapOilLampExplosion01: fire burst + light
	inline std::string frostPotModel = "Skyrim.esm|0B9BDE";        // GlazedJugSmall01
	inline std::string frostExplosionModel = "Skyrim.esm|044089";  // ExplosionRuneFrost01: frost burst + light

	// --- Crafting: what counts as a container / filling ("Plugin.esm|HEXID", fillings ", count") ---
	// Repeatable INI keys (sContainer, sFireFilling, sFrostFilling) add entries; if the INI lists
	// none of a kind, the built-in vanilla lists are used.
	struct Filling
	{
		std::string form;
		int         count = 1;  // how many make one pot
		int         type = 0;   // 0 = Fire Pot, 1 = Frost Pot
	};
	inline std::vector<std::string> containers;
	inline std::vector<Filling>     fillings;

	// --- Bottle return: drinking gives the empty bottle back ---
	inline bool returnEmptyBottles = true;
	inline bool useBuiltInBottles = true;    // the vanilla drink -> bottle list
	inline bool matchBottlesByModel = true;  // other drinks that use an empty container's bottle model
	inline std::vector<std::pair<std::string, std::string>> bottles;  // sBottle = drink -> empty

	// --- Messages ---
	inline bool printToConsole = true;
	inline bool showNotification = true;

	void Load();
}
