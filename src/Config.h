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
	inline bool          craftInCombat = false;  // allow making bombs while in combat
	inline std::uint32_t reloadConfigKey = 0;    // re-read the INI in game
	inline float         minSpeed = 250.0f;      // units/s at the shortest throw (~3.5 m/s)
	inline float         maxSpeed = 835.0f;      // units/s at full wind-up (~12 m/s, ~20 m on flat ground)
	inline float         chargeSeconds = 1.0f;   // time to full wind-up
	inline float         loftDegrees = 12.0f;    // extra upward angle over the crosshair
	inline float         placeReach = 250.0f;    // how far ahead a tripwire can be set down
	inline float         minHoldMs = 150.0f;     // shorter taps cancel (no bomb used)
	inline float         cooldownMs = 600.0f;    // between throws
	inline bool          cancelOnBlock = true;   // pressing block during a G wind-up cancels it
	inline std::string   throwNode = "NPC L Hand [LHnd]";       // where a left-hand / throw-key bomb leaves from
	inline std::string   throwNodeRight = "NPC R Hand [RHnd]";  // where a right-hand bomb leaves from
	inline float         projectileScale = 0.7f;  // size of a bomb in flight / on the ground (1 = the container's size)

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
		"",                   // smoke: none (the cloud is the show)
		"Skyrim.esm|0D13E8",  // fear: ExplosionShockMass01 (flash)
		"Skyrim.esm|0665BB",  // frenzy: ExplosionRuneShock01
		"Skyrim.esm|0FDBBA",  // concussion: TG08BShockwaveExplosion
	};
	inline std::string oilFireModel = "Skyrim.esm|109E74";         // TrapOilExplosionInitial
	// Hazards (lingering or short) take a borrowed model reliably; these are what you see.
	inline std::string smokeCloudModel = "Skyrim.esm|0D6012";      // BFXSmokeBurningVol (debug smoke 5)
	inline std::string smokeMistModel = "Skyrim.esm|0B946C";       // FXMistLowSmokey01 (debug smoke 6)
	inline std::string oilSlickModel = "Dragonborn.esm|01DA00";    // DLC2ExpSpiderOilPool (oil spider's pool)
	inline std::string frostBurstModel = "Skyrim.esm|0B79F9";      // FrostBlizzardSpellHazard01
	inline std::string fearBurstModel = "Skyrim.esm|08CA32";       // FX_AzuraGoodMagicCloud
	inline std::string frenzyBurstModel = "Skyrim.esm|08CA31";     // FX_AzuraEvilMagicCloud
	// A short look dropped where any bomb bursts, per payload ("" = none: frost, fear and frenzy
	// have theirs above, smoke's cloud is enough)
	inline std::string burstLookModel[Bombs::kPayloads]{
		"Skyrim.esm|0B8B0D",  // frag: a dust cloud (FXIrkngthandCollapseDustCloud)
		"Skyrim.esm|108D70",  // fire: a pillar of flame (MGMagicFirePillarSmall)
		"",                   // frost
		"Skyrim.esm|018F7E",  // oil: a splash (FXFloorSplash_Large)
		"",                   // smoke
		"",                   // fear
		"",                   // frenzy
		"Skyrim.esm|10ACDA",  // concussion: a shockwave (MG07LabyrinthianShockwave)
	};
	// The bomb in your hand (casting art, like the spider scrolls), shrunk to fit
	inline float handScale = 0.45f;
	inline float handOffset[3]{ 0.0f, 0.0f, 0.0f };  // x y z on the hand's magic node (fHandOffsetX/Y/Z)

	// --- Debug: make bombs for nothing (nothing used up, no Alchemy practice) ---
	inline bool freeCrafting = true;

	// --- Weights (set at load): what bombs are made of, and the bombs ---
	inline float                    miscIngredientWeight = 0.1f;  // containers and non-alchemy ingredients
	inline float                    bombWeight = 0.5f;
	inline std::vector<std::string> weightless{ "Skyrim.esm|033761" };  // Paper Roll: 0

	// --- Crafting ---
	// Font of the bomb-making menu (a .ttf/.otf; relative to the Skyrim folder). Missing = built-in.
	inline std::string menuFont = "Data/SKSE/Plugins/FirePots/MenuFont.ttf";
	struct Ingredient
	{
		std::vector<std::string> forms;  // "Plugin.esm|HEXID"; more than one = any of them ("A or B")
		int                      count = 1;
		int                      group = 0;  // trigger (for trigger ingredients) or payload (for fillings)
	};
	inline std::vector<std::string> containers;         // any one (sContainer)
	inline std::vector<Ingredient>  triggerIngredients;  // every line needed; "or" = alternatives
	inline std::vector<Ingredient>  fillings;            // any one line per payload (sFragFilling, ...)
	// A bomb's price = what its ingredients are worth (cheapest of each choice) x this
	inline float valueMult = 1.25f;
	// Alchemy skill use per bomb made = its price x this (vanilla potions give their price)
	inline float alchemyXPMult = 1.0f;

	// --- Debug: smoke bombs, one per candidate smoke look ---
	struct DebugSmoke
	{
		std::string form;   // "Plugin.esm|HEXID": a moveable static / activator (the cloud) or an explosion (the burst)
		std::string label;  // shown in the item name
	};
	inline bool                    giveDebugSmokeBombs = false;  // top up to 5 of each on every load
	inline std::vector<DebugSmoke> debugSmokes;                 // sDebugSmoke = form, label (up to 10)

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
