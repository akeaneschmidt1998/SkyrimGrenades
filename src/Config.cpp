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
		else if (key == "ireloadconfigkey")   reloadConfigKey = static_cast<std::uint32_t>(std::stoul(val));
		else if (key == "fminspeed")          minSpeed = (std::max)(50.0f, std::stof(val));
		else if (key == "fmaxspeed")          maxSpeed = (std::max)(50.0f, std::stof(val));
		else if (key == "fchargeseconds")     chargeSeconds = (std::max)(0.05f, std::stof(val));
		else if (key == "floftdegrees")       loftDegrees = std::clamp(std::stof(val), -30.0f, 60.0f);
		else if (key == "fminholdms")         minHoldMs = (std::max)(0.0f, std::stof(val));
		else if (key == "fcooldownms")        cooldownMs = (std::max)(0.0f, std::stof(val));
		else if (key == "bcancelonblock")     cancelOnBlock = ParseBool(val);
		else if (key == "sthrownode")         throwNode = Unquote(val);
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
		else if (key == "spotmodel")          potModel = Unquote(val);
		else if (key == "sexplosionmodel")    explosionModel = Unquote(val);
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
	void Load()
	{
		if (!ReadIni(kMainIni)) {
			logger::warn("{} not found, using defaults", kMainIni.string());
		}
		if (maxSpeed < minSpeed) {
			std::swap(minSpeed, maxSpeed);
		}
		logger::info("Throw key {}, speed {}-{} u/s over {}s, loft {} deg, node '{}'",
			throwKey, minSpeed, maxSpeed, chargeSeconds, loftDegrees, throwNode);
		logger::info("Preview: arc={} ring={} (r={}) gravity={} autoCalibrate={}",
			showArc, showBlastRing, blastRadius, gravityUnits > 0.0f ? std::to_string(gravityUnits) : "learn on first throw"s, autoCalibrate);
	}
}
