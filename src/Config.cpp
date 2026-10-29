#include "Config.h"

#include <algorithm>
#include <cctype>

namespace
{
	const std::filesystem::path kMainIni{ "Data/SKSE/Plugins/FirePots.ini" };
	// Written by MCM Helper when you change something in the Mod Configuration Menu.
	const std::filesystem::path kMcmUserIni{ "Data/MCM/Settings/FirePots.ini" };

	std::string Trim(std::string s)
	{
		auto notSpace = [](unsigned char c) { return !std::isspace(c); };
		s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
		s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
		return s;
	}

	std::string Lower(std::string s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return s;
	}

	bool ParseBool(const std::string& v) { return v == "1" || Lower(v) == "true"; }

	std::string Unquote(std::string v)
	{
		if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
			return v.substr(1, v.size() - 2);
		}
		return v;
	}

	std::vector<std::string> SplitComma(const std::string& a_val)
	{
		std::vector<std::string> parts;
		std::string              rest = a_val;
		for (std::size_t c; (c = rest.find(',')) != std::string::npos; rest.erase(0, c + 1)) {
			parts.push_back(Trim(rest.substr(0, c)));
		}
		parts.push_back(Trim(rest));
		return parts;
	}

	// "Plugin.esm|ID or Plugin.esm|ID, 2" -> alternatives, count
	Config::Ingredient ParseIngredient(const std::string& a_val, int a_group)
	{
		const auto parts = SplitComma(a_val);
		Config::Ingredient i;
		std::string alts = Unquote(parts[0]) + " or ";
		for (std::size_t at; (at = Lower(alts).find(" or ")) != std::string::npos; alts.erase(0, at + 4)) {
			if (auto f = Trim(alts.substr(0, at)); !f.empty()) {
				i.forms.push_back(f);
			}
		}
		if (i.forms.empty()) {
			throw std::invalid_argument("no form");
		}
		i.count = parts.size() > 1 ? std::clamp(std::stoi(parts[1]), 1, 99) : 1;
		i.group = a_group;
		return i;
	}

	bool weightlessCleared = false;  // the first sWeightless line replaces the built-in list

	// "fire impact mild" (any order; prefixes ok) -> bomb index
	int ParseBomb(const std::string& a_spec)
	{
		int        trigger = -1, payload = -1, strength = -1;
		std::string word;
		std::string spec = a_spec + " ";
		for (char c : spec) {
			if (std::isspace(static_cast<unsigned char>(c))) {
				if (!word.empty()) {
					const auto w = Lower(word);
					if (w == "fuse") {
						// "short fuse" / "long fuse": the first word already decided it
					} else if (auto p = Bombs::Match(w, Bombs::kPayloadNames); p && payload < 0) {
						payload = *p;
					} else if (auto t = Bombs::Match(w, Bombs::kTriggerNames); t && trigger < 0) {
						trigger = *t;
					} else if (auto s = Bombs::Match(w, Bombs::kStrengthNames); s && strength < 0) {
						strength = *s;
					}
					word.clear();
				}
			} else {
				word += c;
			}
		}
		if (trigger < 0 || payload < 0 || strength < 0) {
			throw std::invalid_argument("want payload trigger strength, e.g. 'fire impact mild'");
		}
		return Bombs::Index(trigger, payload, strength);
	}

	void Apply(const std::string& key, const std::string& val)
	{
		using namespace Config;
		// repeatable list keys first
		if (key == "scontainer") {
			containers.push_back(Unquote(val));
			return;
		}
		for (int t = 0; t < Bombs::kTriggers; ++t) {
			auto name = Lower(std::string(Bombs::kTriggerNames[t]));
			std::erase(name, ' ');
			if (key == "s" + name + "ingredient") {
				triggerIngredients.push_back(ParseIngredient(val, t));
				return;
			}
		}
		for (int p = 0; p < Bombs::kPayloads; ++p) {
			if (key == "s" + Lower(std::string(Bombs::kPayloadNames[p])) + "filling") {
				if (Bombs::Available(p)) {
					fillings.push_back(ParseIngredient(val, p));
				}
				return;
			}
			if (key == "s" + Lower(std::string(Bombs::kPayloadNames[p])) + "burstlookmodel") {
				burstLookModel[p] = Unquote(val);
				return;
			}
			if (key == "s" + Lower(std::string(Bombs::kPayloadNames[p])) + "explosionmodel") {
				explosionModel[p] = Unquote(val);
				return;
			}
		}
		for (int t = 0; t < Bombs::kTriggers; ++t) {
			auto name = Lower(std::string(Bombs::kTriggerNames[t]));
			std::erase(name, ' ');
			if (key == "s" + name + "model") {
				triggerModel[t] = Unquote(val);
				return;
			}
		}
		// clang-format off
		if      (key == "ithrowkey")          throwKey = static_cast<std::uint32_t>(std::stoul(val));
		else if (key == "icyclekey")          cycleKey = static_cast<std::uint32_t>(std::stoul(val));
		else if (key == "icraftkey")          craftKey = static_cast<std::uint32_t>(std::stoul(val));
		else if (key == "bcraftincombat")     craftInCombat = ParseBool(val);
		else if (key == "ireloadconfigkey")   reloadConfigKey = static_cast<std::uint32_t>(std::stoul(val));
		else if (key == "fminspeed")          minSpeed = (std::max)(50.0f, std::stof(val));
		else if (key == "fmaxspeed")          maxSpeed = (std::max)(50.0f, std::stof(val));
		else if (key == "fchargeseconds")     chargeSeconds = (std::max)(0.05f, std::stof(val));
		else if (key == "floftdegrees")       loftDegrees = std::clamp(std::stof(val), -30.0f, 60.0f);
		else if (key == "fplacereach")        placeReach = std::clamp(std::stof(val), 50.0f, 1000.0f);
		else if (key == "fminholdms")         minHoldMs = (std::max)(0.0f, std::stof(val));
		else if (key == "fcooldownms")        cooldownMs = (std::max)(0.0f, std::stof(val));
		else if (key == "bcancelonblock")     cancelOnBlock = ParseBool(val);
		else if (key == "sthrownode")         throwNode = Unquote(val);
		else if (key == "sthrownoderight")    throwNodeRight = Unquote(val);
		else if (key == "fprojectilescale")   projectileScale = std::clamp(std::stof(val), 0.05f, 5.0f);
		else if (key == "bshowarc")           showArc = ParseBool(val);
		else if (key == "farcstep")           arcStep = std::clamp(std::stof(val), 0.005f, 0.2f);
		else if (key == "farcmaxtime")        arcMaxTime = std::clamp(std::stof(val), 0.5f, 10.0f);
		else if (key == "bshowblastring")     showBlastRing = ParseBool(val);
		else if (key == "farcthickness")      arcThickness = std::clamp(std::stof(val), 0.5f, 20.0f);
		else if (key == "farcopacity")        arcOpacity = std::clamp(std::stof(val), 0.0f, 1.0f);
		else if (key == "bshowchargemeter")   showChargeMeter = ParseBool(val);
		else if (key == "fgravityunits")      gravityUnits = (std::max)(0.0f, std::stof(val));
		else if (key == "bautocalibrate")     autoCalibrate = ParseBool(val);
		else if (key == "bcalibratespeed")    calibrateSpeed = ParseBool(val);
		else if (key == "ssmokecloudmodel")   smokeCloudModel = Unquote(val);
		else if (key == "ssmokemistmodel")    smokeMistModel = Unquote(val);
		else if (key == "soilslickmodel")     oilSlickModel = Unquote(val);
		else if (key == "sfrostburstmodel")   frostBurstModel = Unquote(val);
		else if (key == "sfearburstmodel")    fearBurstModel = Unquote(val);
		else if (key == "sfrenzyburstmodel")  frenzyBurstModel = Unquote(val);
		else if (key == "fhandscale")         handScale = std::clamp(std::stof(val), 0.05f, 3.0f);
		else if (key == "fhandoffsetx")       handOffset[0] = std::clamp(std::stof(val), -100.0f, 100.0f);
		else if (key == "fhandoffsety")       handOffset[1] = std::clamp(std::stof(val), -100.0f, 100.0f);
		else if (key == "fhandoffsetz")       handOffset[2] = std::clamp(std::stof(val), -100.0f, 100.0f);
		else if (key == "fhandrotx")          handRotation[0] = std::clamp(std::stof(val), -360.0f, 360.0f);
		else if (key == "fhandroty")          handRotation[1] = std::clamp(std::stof(val), -360.0f, 360.0f);
		else if (key == "fhandrotz")          handRotation[2] = std::clamp(std::stof(val), -360.0f, 360.0f);
		else if (key == "sfireblazemodel")    fireBlazeModel = Unquote(val);
		else if (key == "bfreecrafting")      freeCrafting = ParseBool(val);
		else if (key == "fmiscingredientweight") miscIngredientWeight = std::clamp(std::stof(val), -1.0f, 100.0f);
		else if (key == "fbombweight")        bombWeight = std::clamp(std::stof(val), -1.0f, 100.0f);
		else if (key == "sweightless") {
			if (!weightlessCleared) {
				weightless.clear();
				weightlessCleared = true;
			}
			weightless.push_back(Unquote(val));
		}
		else if (key == "smenufont")          menuFont = Unquote(val);
		else if (key == "fvaluemult")         valueMult = std::clamp(std::stof(val), 0.0f, 100.0f);
		else if (key == "falchemyxpmult")     alchemyXPMult = std::clamp(std::stof(val), 0.0f, 100.0f);
		else if (key == "bgivedebugsmokebombs") giveDebugSmokeBombs = ParseBool(val);
		else if (key == "sdebugsmoke") {
			const auto parts = SplitComma(val);
			debugSmokes.push_back({ Unquote(parts[0]), parts.size() > 1 ? parts[1] : parts[0] });
		}
		else if (key == "soilfiremodel")      oilFireModel = Unquote(val);
		else if (key == "breturnemptybottles")  returnEmptyBottles = ParseBool(val);
		else if (key == "busebuiltinbottles")   useBuiltInBottles = ParseBool(val);
		else if (key == "bmatchbottlesbymodel") matchBottlesByModel = ParseBool(val);
		else if (key == "sbottle") {
			const auto arrow = val.find("->");
			if (arrow == std::string::npos) {
				throw std::invalid_argument("want drink -> empty");
			}
			bottles.emplace_back(Trim(val.substr(0, arrow)), Trim(val.substr(arrow + 2)));
		}
		else if (key == "baddtoleveledlists")   addToLeveledLists = ParseBool(val);
		else if (key == "sloot") {
			const auto parts = SplitComma(val);
			if (parts.size() != 4) {
				throw std::invalid_argument("want list, payload trigger strength, level, count");
			}
			Loot l;
			l.list = parts[0];
			l.bomb = ParseBomb(parts[1]);
			if (!Bombs::Available(Bombs::PayloadOf(l.bomb))) {
				logger::info("sLoot '{}': that payload is out for now, skipped", parts[1]);
				return;
			}
			l.level = std::clamp(std::stoi(parts[2]), 1, 255);
			l.count = std::clamp(std::stoi(parts[3]), 1, 100);
			loot.push_back(std::move(l));
		}
		else if (key == "bprinttoconsole")    printToConsole = ParseBool(val);
		else if (key == "bshownotification")  showNotification = ParseBool(val);
		else logger::warn("Unknown INI key '{}'", key);
		// clang-format on
	}

	bool ReadIni(const std::filesystem::path& a_path)
	{
		std::ifstream file(a_path);
		if (!file) {
			return false;
		}
		std::string line;
		while (std::getline(file, line)) {
			const auto comment = line.find_first_of(";#");
			if (comment != std::string::npos) {
				line.erase(comment);
			}
			const auto eq = line.find('=');
			if (eq == std::string::npos) {
				continue;  // blank line or [Section] header
			}
			const auto key = Lower(Trim(line.substr(0, eq)));
			const auto val = Trim(line.substr(eq + 1));
			if (val.empty()) {
				continue;
			}
			try {
				Apply(key, val);
			} catch (const std::exception&) {
				logger::warn("{}: bad value for '{}': '{}'", a_path.filename().string(), key, val);
			}
		}
		return true;
	}

	void Defaults()
	{
		using namespace Config;
		if (containers.empty()) {
			containers = {
				"Skyrim.esm|0F2012", "Skyrim.esm|0F2013", "Skyrim.esm|0F2014", "Skyrim.esm|0F2015",  // empty wine bottles
				"Skyrim.esm|0FED17",                                                                // empty spiced wine bottle
				"Dawnguard.esm|0093F5", "Dawnguard.esm|0093F6",                                    // empty wine bottles
				"Skyrim.esm|0B9BDE", "Skyrim.esm|0B9BD6", "Skyrim.esm|0F08F3", "Skyrim.esm|0F08F1",  // jugs
				"Skyrim.esm|08632C",                                                                // glazed jug
				"Skyrim.esm|0B9BD2", "Skyrim.esm|0B9BD4", "Skyrim.esm|0F08F6", "Skyrim.esm|0F08F7",  // pots
			};
		}
		auto noneFor = [](const std::vector<Ingredient>& a_list, int a_group) {
			return std::ranges::none_of(a_list, [&](const Ingredient& i) { return i.group == a_group; });
		};
		using B = Bombs::Trigger;
		const std::vector<Ingredient> triggerDefaults{
			{ { "Skyrim.esm|03AD5E" }, 1, B::kImpact },     // Fire Salts
			{ { "Skyrim.esm|033761" }, 1, B::kShortFuse },  // Paper Roll
			{ { "Skyrim.esm|033760" }, 1, B::kShortFuse },  // Charcoal
			{ { "Skyrim.esm|033761" }, 2, B::kLongFuse },   // Paper Roll
			{ { "Skyrim.esm|033760" }, 1, B::kLongFuse },   // Charcoal
			// the wire: Linen Wrap, Leather Strips or Leather
			{ { "Skyrim.esm|034CD6", "Skyrim.esm|0800E4", "Skyrim.esm|0DB5D2" }, 1, B::kTripwire },
			// the igniter: Fire Salts, or Dwemer clockwork (a gear or a gyro)
			{ { "Skyrim.esm|03AD5E", "Skyrim.esm|0C8861", "Skyrim.esm|0C8868" }, 1, B::kTripwire },
		};
		for (int t = 0; t < Bombs::kTriggers; ++t) {
			if (noneFor(triggerIngredients, t)) {
				for (const auto& i : triggerDefaults) {
					if (i.group == t) triggerIngredients.push_back(i);
				}
			}
		}
		using P = Bombs::Payload;
		const std::vector<Ingredient> fillingDefaults{
			{ { "HearthFires.esm|00300F" }, 1, P::kFrag },        // Nails
			{ { "HearthFires.esm|00306C" }, 1, P::kFrag },        // Quarried Stone
			{ { "Skyrim.esm|067181" }, 1, P::kFrag },             // Soul Gem Fragment (all five)
			{ { "Skyrim.esm|067182" }, 1, P::kFrag },
			{ { "Skyrim.esm|067183" }, 1, P::kFrag },
			{ { "Skyrim.esm|067184" }, 1, P::kFrag },
			{ { "Skyrim.esm|067185" }, 1, P::kFrag },
			{ { "Skyrim.esm|03AD5E" }, 1, P::kFire },             // Fire Salts
			{ { "Dragonborn.esm|01CD74" }, 2, P::kFire },         // Ash Creep Cluster
			{ { "Dragonborn.esm|01CD6E" }, 2, P::kFire },         // Burnt Spriggan Wood
			{ { "Dragonborn.esm|016E26" }, 2, P::kFire },         // Spawn Ash
			{ { "Skyrim.esm|03AD5F" }, 1, P::kFrost },            // Frost Salts
			{ { "Skyrim.esm|03AD6A" }, 1, P::kFrost },            // Ice Wraith Teeth
			{ { "Skyrim.esm|034D32" }, 2, P::kFrost },            // Frost Mirriam
			{ { "Skyrim.esm|0F11C0" }, 1, P::kOil },              // Dwarven Oil
			{ { "Skyrim.esm|03AD72" }, 1, P::kOil },              // Troll Fat
			{ { "Dragonborn.esm|01CD71" }, 2, P::kOil },          // Ash Hopper Jelly
			{ { "Skyrim.esm|03AD63" }, 1, P::kFear },             // Ectoplasm
			{ { "Skyrim.esm|03AD73" }, 1, P::kFear },             // Glow Dust
			{ { "Skyrim.esm|02F44C" }, 2, P::kFear },             // Nightshade
			{ { "Skyrim.esm|0516C8" }, 2, P::kFear },             // Deathbell
			{ { "Skyrim.esm|03AD66" }, 1, P::kFrenzy },           // Hagraven Feathers
			{ { "Skyrim.esm|03AD5D" }, 1, P::kFrenzy },           // Falmer Ear
			{ { "Skyrim.esm|06BC10" }, 2, P::kFrenzy },           // Powdered Mammoth Tusk
			{ { "Skyrim.esm|03AD60" }, 1, P::kConcussion },       // Void Salts
			{ { "Skyrim.esm|03AD61" }, 1, P::kConcussion },       // Briar Heart
			{ { "Skyrim.esm|034CDD" }, 2, P::kConcussion },       // Bone Meal
		};
		for (int p = 0; p < Bombs::kPayloads; ++p) {
			if (noneFor(fillings, p)) {
				for (const auto& i : fillingDefaults) {
					if (i.group == p) fillings.push_back(i);
				}
			}
		}
		if (loot.empty()) {
			using S = Bombs::Strength;
			auto L = [](const char* a_list, int t, int p, int s, int level, int count) {
				return Loot{ a_list, Bombs::Index(t, p, s), level, count };
			};
			loot = {
				// general goods merchants
				L("Skyrim.esm|09AF0A", B::kImpact, P::kFire, S::kMild, 1, 3),
				L("Skyrim.esm|09AF0A", B::kShortFuse, P::kFrag, S::kMild, 1, 2),
				L("Skyrim.esm|09AF0A", B::kImpact, P::kOil, S::kMild, 1, 2),
				L("Skyrim.esm|09AF0A", B::kTripwire, P::kFrag, S::kModerate, 10, 1),
				// apothecaries
				L("Skyrim.esm|09AF13", B::kImpact, P::kFire, S::kMild, 1, 2),
				L("Skyrim.esm|09AF13", B::kImpact, P::kFrost, S::kMild, 5, 2),
				L("Skyrim.esm|09AF13", B::kImpact, P::kFear, S::kMild, 5, 1),
				L("Skyrim.esm|09AF13", B::kImpact, P::kFrenzy, S::kMild, 10, 1),
				// court wizards' scroll stock
				L("Skyrim.esm|105240", B::kImpact, P::kConcussion, S::kModerate, 10, 1),
				L("Skyrim.esm|105240", B::kShortFuse, P::kFrost, S::kModerate, 10, 1),
				L("Skyrim.esm|105240", B::kImpact, P::kFear, S::kModerate, 15, 1),
				// bandit and Forsworn random loot
				L("Skyrim.esm|10E0DD", B::kImpact, P::kFire, S::kMild, 1, 1),
				L("Skyrim.esm|10E0DD", B::kShortFuse, P::kFrag, S::kMild, 5, 1),
				L("Skyrim.esm|10FAB6", B::kImpact, P::kFire, S::kMild, 1, 1),
				L("Skyrim.esm|10FAB6", B::kTripwire, P::kFrag, S::kMild, 5, 1),
				// random potion loot (LItemPotionAll: chests, barrels, enemies' potions)
				L("Skyrim.esm|0E3E99", B::kImpact, P::kFire, S::kMild, 1, 1),
				L("Skyrim.esm|0E3E99", B::kImpact, P::kFrost, S::kMild, 1, 1),
				L("Skyrim.esm|0E3E99", B::kShortFuse, P::kFrag, S::kMild, 1, 1),
				L("Skyrim.esm|0E3E99", B::kImpact, P::kOil, S::kMild, 3, 1),
				L("Skyrim.esm|0E3E99", B::kImpact, P::kFrenzy, S::kMild, 5, 1),
				L("Skyrim.esm|0E3E99", B::kImpact, P::kFear, S::kModerate, 12, 1),
				L("Skyrim.esm|0E3E99", B::kImpact, P::kConcussion, S::kModerate, 15, 1),
			};
		}
		if (debugSmokes.empty()) {
			debugSmokes = {
				{ "Skyrim.esm|0B949F", "Smoke, large volume" },          // FXSmokeSmokeLgVol
				{ "Skyrim.esm|0B9496", "Smoke, large 1x1" },             // FXSmokeSmokeLg1x1
				{ "Skyrim.esm|01643C", "Smoke wisps" },                  // FXSmokeWispsLgVolAdjust
				{ "Skyrim.esm|0D5E82", "Smoke, large close" },           // FXSmokeLargeClose01
				{ "Skyrim.esm|0D6012", "Burning smoke" },                // BFXSmokeBurningVol
				{ "Skyrim.esm|0B946C", "Low smoky mist" },               // FXMistLowSmokey01
				{ "Skyrim.esm|030B39", "Steam billow" },                 // FXSteamBillow
				{ "Skyrim.esm|0B93DB", "Wet mist volume" },              // FXSmokeWetMistLgVol01
				{ "Dragonborn.esm|026629", "Steam burst (explosion)" },  // DLC2KagrumezSteamExplosion
				{ "Skyrim.esm|0A036F", "Gas trap (explosion)" },         // TrapPoisonGasExplosion01
			};
		}
	}
}

namespace Config
{
	void Load()
	{
		containers.clear();
		triggerIngredients.clear();
		fillings.clear();
		bottles.clear();
		loot.clear();
		debugSmokes.clear();
		weightlessCleared = false;
		weightless = { "Skyrim.esm|033761" };
		if (!ReadIni(kMainIni)) {
			logger::warn("{} not found, using defaults", kMainIni.string());
		}
		// Values changed in the MCM override the main INI.
		if (ReadIni(kMcmUserIni)) {
			logger::info("Applied MCM settings from {}", kMcmUserIni.string());
		}
		Defaults();
		if (maxSpeed < minSpeed) {
			std::swap(minSpeed, maxSpeed);
		}
		logger::info("Keys: throw {}, cycle {}, craft {}, reload {}", throwKey, cycleKey, craftKey, reloadConfigKey);
		logger::info("Throw: {}-{} u/s over {}s, loft {} deg, tripwire reach {}", minSpeed, maxSpeed, chargeSeconds, loftDegrees, placeReach);
		logger::info("Preview: arc={} ring={} meter={} gravity={} autoCalibrate={}", showArc, showBlastRing, showChargeMeter,
			gravityUnits > 0.0f ? std::to_string(gravityUnits) : "learn on first throw"s, autoCalibrate);
		logger::info("Crafting: {} containers, {} trigger ingredients, {} fillings, in combat: {}", containers.size(),
			triggerIngredients.size(), fillings.size(), craftInCombat);
	}
}
