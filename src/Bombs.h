#pragma once
// The bomb catalogue: trigger x payload x strength, numbered exactly as tools/MakePlugin does.
// Plain std only.
//
// Two numberings:
//   dense index  (t * kPayloads + p) * kStrengths + s      - Forms::Types() order, loot, menus
//   form slot    (t * 16 + p) * 3 + s                      - fixed form IDs in FirePots.esp; leaves
//                                                            room for 8 triggers x 16 payloads so
//                                                            adding one never moves existing IDs

#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace Bombs
{
	enum Trigger : int { kImpact, kShortFuse, kLongFuse, kTripwire, kTriggers };
	enum Payload : int { kFrag, kFire, kFrost, kOil, kSmoke, kFear, kFrenzy, kConcussion, kPayloads };
	enum Strength : int { kMild, kModerate, kMajor, kStrengths };

	inline constexpr int kT = kTriggers, kP = kPayloads, kS = kStrengths;
	inline constexpr int kCount = kT * kP * kS;  // 96

	inline constexpr std::array<std::string_view, kT> kTriggerNames{ "Impact", "Short Fuse", "Long Fuse", "Tripwire" };
	inline constexpr std::array<std::string_view, kP> kPayloadNames{ "Frag", "Fire", "Frost", "Oil", "Smoke", "Fear", "Frenzy", "Concussion" };
	inline constexpr std::array<std::string_view, kS> kStrengthNames{ "Mild", "Moderate", "Major" };

	constexpr int Index(int a_trigger, int a_payload, int a_strength) { return (a_trigger * kP + a_payload) * kS + a_strength; }
	constexpr int TriggerOf(int a_index) { return a_index / (kP * kS); }
	constexpr int PayloadOf(int a_index) { return (a_index / kS) % kP; }
	constexpr int StrengthOf(int a_index) { return a_index % kS; }

	// Local form IDs in FirePots.esp
	constexpr unsigned Slot(int a_index) { return static_cast<unsigned>((TriggerOf(a_index) * 16 + PayloadOf(a_index)) * 3 + StrengthOf(a_index)); }
	constexpr unsigned ItemId(int a_index) { return 0x800u + Slot(a_index); }
	constexpr unsigned ItemEffectId(int a_index) { return 0x980u + Slot(a_index); }
	constexpr unsigned ProjectileId(int a_index) { return 0xB00u + Slot(a_index); }
	constexpr unsigned ExplosionId(int a_payload, int a_strength) { return 0xC80u + static_cast<unsigned>(a_payload * 3 + a_strength); }
	constexpr unsigned EnchantId(int a_payload, int a_strength) { return 0xCB0u + static_cast<unsigned>(a_payload * 3 + a_strength); }
	constexpr unsigned SmokeCloudId(int a_strength) { return 0xCE0u + static_cast<unsigned>(a_strength); }
	inline constexpr unsigned kOilFireProjectileId = 0xCF4;

	// Blast radius per strength (the ring the preview draws); concussion is 1.75x wider.
	constexpr float BlastRadius(int a_payload, int a_strength)
	{
		const float r = 140.0f + 70.0f * static_cast<float>(a_strength);
		return a_payload == kConcussion ? r * 1.75f : r;
	}
	constexpr float OilSlickRadius(int a_strength) { return 120.0f + 60.0f * static_cast<float>(a_strength); }
	inline constexpr float kTripwireRadius = 120.0f;  // proximity trigger (ESP)

	// Fuse bombs are real physics objects (they bounce and roll); the fuse is lit when thrown.
	constexpr float FuseSeconds(int a_trigger) { return a_trigger == kShortFuse ? 2.0f : a_trigger == kLongFuse ? 4.5f : 0.0f; }
	constexpr bool  IsFuse(int a_trigger) { return a_trigger == kShortFuse || a_trigger == kLongFuse; }

	// Payloads in the game. Smoke is out until there's a real smoke effect to show (see README, To do);
	// its form IDs stay reserved so nothing else moves.
	constexpr bool Available(int a_payload) { return a_payload != kSmoke; }

	inline std::string Name(int a_index)
	{
		return std::string(kStrengthNames[StrengthOf(a_index)]) + " " + std::string(kPayloadNames[PayloadOf(a_index)]) +
		       " Bomb (" + std::string(kTriggerNames[TriggerOf(a_index)]) + ")";
	}

	// The item description (also written into the plugin by tools/MakePlugin).
	inline std::string Describe(int a_trigger, int a_payload, int a_strength)
	{
		const auto lvl = std::to_string(10 * (a_strength + 1));
		const char* how = a_trigger == kImpact    ? "Shatters on impact into" :
		                  a_trigger == kShortFuse ? "Bounces and rolls; 2 seconds after the throw it bursts into" :
		                  a_trigger == kLongFuse  ? "Bounces and rolls; 4.5 seconds after the throw it bursts into" :
		                                            "Set it down; when someone comes close it bursts into";
		std::string what;
		switch (a_payload) {
		case kFrag: what = "a burst of shrapnel"; break;
		case kFire: what = "a burst of flame that leaves the ground burning"; break;
		case kFrost: what = "a burst of cold that slows and saps stamina"; break;
		case kOil: what = "a slick of oil that slows anyone in it and burns when a fire bomb lands in it"; break;
		case kSmoke: what = "a lingering cloud that hides anyone inside until they strike, but chokes their stamina"; break;
		case kFear: what = "a blinding flash that sends anyone up to level " + lvl + " fleeing"; break;
		case kFrenzy: what = "a cloud that drives anyone up to level " + lvl + " into a frenzy"; break;
		default: what = "a deafening blast that staggers everyone around it"; break;
		}
		return std::string(how) + " " + what + ".";
	}

	// Case-insensitive prefix match of a word against a name list ("short" -> Short Fuse).
	template <std::size_t N>
	std::optional<int> Match(std::string_view a_word, const std::array<std::string_view, N>& a_names)
	{
		if (a_word.empty()) {
			return std::nullopt;
		}
		for (std::size_t i = 0; i < N; ++i) {
			const auto& n = a_names[i];
			if (a_word.size() <= n.size()) {
				bool same = true;
				for (std::size_t k = 0; k < a_word.size() && same; ++k) {
					same = std::tolower(static_cast<unsigned char>(a_word[k])) == std::tolower(static_cast<unsigned char>(n[k]));
				}
				if (same) {
					return static_cast<int>(i);
				}
			}
		}
		return std::nullopt;
	}
}
