#include "Config.h"

#include <algorithm>
#include <cctype>

namespace
{
	const std::filesystem::path kMainIni{ "Data/SKSE/Plugins/FirePots.ini" };

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

	void Apply(const std::string& key, const std::string& val)
	{
		using namespace Config;
		// clang-format off
		if      (key == "ithrowkey")          throwKey = static_cast<std::uint32_t>(std::stoul(val));
		else if (key == "icraftkey")          craftKey = static_cast<std::uint32_t>(std::stoul(val));
		else if (key == "bcraftincombat")     craftInCombat = ParseBool(val);
		else if (key == "scontainer")         containers.push_back(Unquote(val));
		else if (key == "sfirefilling" || key == "sfrostfilling" || key == "ssmokefilling" || key == "sthunderfilling") {
			Filling f;
			const auto comma = val.find(',');
			f.form = Trim(Unquote(val.substr(0, comma)));
			f.count = comma == std::string::npos ? 1 : std::clamp(std::stoi(val.substr(comma + 1)), 1, 99);
			f.type = TypeFromWord(key.substr(1, key.size() - 8));  // "sSmokeFilling" -> "smoke"
			fillings.push_back(std::move(f));
		}
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
			// list, fire|frost, level, count
			std::vector<std::string> parts;
			std::string               rest = val;
			for (std::size_t c; (c = rest.find(',')) != std::string::npos; rest.erase(0, c + 1)) {
				parts.push_back(Trim(rest.substr(0, c)));
			}
			parts.push_back(Trim(rest));
			if (parts.size() != 4) {
				throw std::invalid_argument("want list, fire|frost, level, count");
			}
			Loot l;
			l.list = parts[0];
			l.type = TypeFromWord(parts[1]);
			if (l.type < 0) {
				throw std::invalid_argument("type must be fire, frost, smoke or thunder");
			}
			l.level = std::clamp(std::stoi(parts[2]), 1, 255);
			l.count = std::clamp(std::stoi(parts[3]), 1, 100);
			loot.push_back(std::move(l));
		}
		else if (key == "icyclekey")          cycleKey = static_cast<std::uint32_t>(std::stoul(val));
		else if (key == "ireloadconfigkey")   reloadConfigKey = static_cast<std::uint32_t>(std::stoul(val));
		else if (key == "fminspeed")          minSpeed = (std::max)(50.0f, std::stof(val));
		else if (key == "fmaxspeed")          maxSpeed = (std::max)(50.0f, std::stof(val));
		else if (key == "fchargeseconds")     chargeSeconds = (std::max)(0.05f, std::stof(val));
		else if (key == "floftdegrees")       loftDegrees = std::clamp(std::stof(val), -30.0f, 60.0f);
		else if (key == "fminholdms")         minHoldMs = (std::max)(0.0f, std::stof(val));
		else if (key == "fcooldownms")        cooldownMs = (std::max)(0.0f, std::stof(val));
		else if (key == "bcancelonblock")     cancelOnBlock = ParseBool(val);
		else if (key == "sthrownode")         throwNode = Unquote(val);
		else if (key == "sthrownoderight")    throwNodeRight = Unquote(val);
		else if (key == "bcalibratespeed")    calibrateSpeed = ParseBool(val);
		else if (key == "fprojectilescale")   projectileScale = std::clamp(std::stof(val), 0.05f, 5.0f);
		else if (key == "bshowarc")           showArc = ParseBool(val);
		else if (key == "farcstep")           arcStep = std::clamp(std::stof(val), 0.005f, 0.2f);
		else if (key == "farcmaxtime")        arcMaxTime = std::clamp(std::stof(val), 0.5f, 10.0f);
		else if (key == "fblastradius")       blastRadius = (std::max)(0.0f, std::stof(val));
		else if (key == "bshowblastring")     showBlastRing = ParseBool(val);
		else if (key == "farcthickness")      arcThickness = std::clamp(std::stof(val), 0.5f, 20.0f);
		else if (key == "farcopacity")        arcOpacity = std::clamp(std::stof(val), 0.0f, 1.0f);
		else if (key == "bshowchargemeter")   showChargeMeter = ParseBool(val);
		else if (key == "fgravityunits")      gravityUnits = (std::max)(0.0f, std::stof(val));
		else if (key == "bautocalibrate")     autoCalibrate = ParseBool(val);
		else if (key == "sfirepotmodel" || key == "spotmodel")             firePotModel = Unquote(val);
		else if (key == "sfireexplosionmodel" || key == "sexplosionmodel") fireExplosionModel = Unquote(val);
		else if (key == "sfrostpotmodel")       frostPotModel = Unquote(val);
		else if (key == "sfrostexplosionmodel") frostExplosionModel = Unquote(val);
		else if (key == "ssmokepotmodel")       smokePotModel = Unquote(val);
		else if (key == "ssmokeexplosionmodel") smokeExplosionModel = Unquote(val);
		else if (key == "ssmokecloudmodel")     smokeCloudModel = Unquote(val);
		else if (key == "sthunderpotmodel")     thunderPotModel = Unquote(val);
		else if (key == "sthunderexplosionmodel") thunderExplosionModel = Unquote(val);
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
}

namespace Config
{
	int TypeFromWord(const std::string& a_word)
	{
		const auto w = Lower(Trim(a_word));
		if (w.starts_with("fire")) return 0;
		if (w.starts_with("frost")) return 1;
		if (w.starts_with("smoke")) return 2;
		if (w.starts_with("thunder")) return 3;
		return -1;
	}

	void Load()
	{
		containers.clear();
		fillings.clear();
		bottles.clear();
		loot.clear();
		if (!ReadIni(kMainIni)) {
			logger::warn("{} not found, using defaults", kMainIni.string());
		}
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
		if (std::ranges::none_of(fillings, [](const Filling& f) { return f.type == 0; })) {
			for (auto& [form, count] : std::initializer_list<std::pair<const char*, int>>{
					 { "Skyrim.esm|03AD5E", 1 },      // Fire Salts
					 { "Skyrim.esm|0F11C0", 1 },      // Dwarven Oil
					 { "Skyrim.esm|03AD72", 1 },      // Troll Fat
					 { "Skyrim.esm|052695", 2 },      // Charred Skeever Hide
					 { "Skyrim.esm|063B5F", 2 },      // Spriggan Sap
					 { "Dragonborn.esm|01CD74", 2 },  // Ash Creep Cluster
					 { "Dragonborn.esm|01CD71", 2 },  // Ash Hopper Jelly
					 { "Dragonborn.esm|01CD6E", 2 },  // Burnt Spriggan Wood
					 { "Dragonborn.esm|016E26", 2 },  // Spawn Ash
				 }) {
				fillings.push_back({ form, count, 0 });
			}
		}
		if (std::ranges::none_of(fillings, [](const Filling& f) { return f.type == 1; })) {
			fillings.push_back({ "Skyrim.esm|03AD5F", 1, 1 });  // Frost Salts
			fillings.push_back({ "Skyrim.esm|03AD6A", 1, 1 });  // Ice Wraith Teeth
		}
		if (std::ranges::none_of(fillings, [](const Filling& f) { return f.type == 2; })) {
			fillings.push_back({ "Skyrim.esm|033760", 1, 2 });  // Charcoal
			fillings.push_back({ "Skyrim.esm|03F7F8", 2, 2 });  // Tundra Cotton
		}
		if (std::ranges::none_of(fillings, [](const Filling& f) { return f.type == 3; })) {
			fillings.push_back({ "Skyrim.esm|03AD60", 1, 3 });  // Void Salts
			fillings.push_back({ "Skyrim.esm|03AD73", 1, 3 });  // Glow Dust
			fillings.push_back({ "Skyrim.esm|07EE01", 2, 3 });  // Glowing Mushroom
		}
		if (loot.empty()) {
			loot = {
				{ "Skyrim.esm|09AF0A", 0, 1, 3 },   // general goods merchants
				{ "Skyrim.esm|09AF0A", 1, 10, 2 },
				{ "Skyrim.esm|09AF13", 0, 1, 2 },   // apothecaries
				{ "Skyrim.esm|09AF13", 1, 1, 2 },
				{ "Skyrim.esm|105240", 1, 1, 2 },   // court wizards' scroll stock
				{ "Skyrim.esm|10E0DD", 0, 1, 1 },   // bandit random loot
				{ "Skyrim.esm|10FAB6", 0, 1, 1 },   // Forsworn random loot
				{ "Skyrim.esm|09AF0A", 2, 5, 2 },   // smoke: general goods
				{ "Skyrim.esm|10E0DD", 2, 5, 1 },   // smoke: bandits
				{ "Skyrim.esm|09AF13", 3, 10, 1 },  // thunder: apothecaries
				{ "Skyrim.esm|105240", 3, 10, 1 },  // thunder: court wizards
			};
		}
		logger::info("Crafting: key {}, {} containers, {} fillings, in combat: {}", craftKey, containers.size(), fillings.size(), craftInCombat);

		if (maxSpeed < minSpeed) {
			std::swap(minSpeed, maxSpeed);
		}
		logger::info("Cycle key {}", cycleKey);
		logger::info("Throw key {}, speed {}-{} u/s over {}s, loft {} deg, node '{}'",
			throwKey, minSpeed, maxSpeed, chargeSeconds, loftDegrees, throwNode);
		logger::info("Preview: arc={} ring={} (r={}) gravity={} autoCalibrate={}",
			showArc, showBlastRing, blastRadius, gravityUnits > 0.0f ? std::to_string(gravityUnits) : "learn on first throw"s, autoCalibrate);
	}
}
