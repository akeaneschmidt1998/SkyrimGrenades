#pragma once
// The bomb catalogue: trigger x payload x strength, numbered exactly as tools/MakePlugin does,
// and the numbers the descriptions quote (kept in step with the plugin's records by hand: if you
// change a damage or radius here, change it in tools/MakePlugin/Program.cs too). Plain std only.
//
// Two numberings:
//   dense index  (t * kPayloads + p) * kStrengths + s      - Forms::Types() order, loot, menus
//   form slot    (t * 16 + p) * 3 + s                      - fixed form IDs in FirePots.esp; leaves
//                                                            room for 8 triggers x 16 payloads so
//                                                            adding one never moves existing IDs

#include <array>
#include <cctype>
#include <cstdio>
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
	inline constexpr unsigned kOilFireProjectileId = 0xCF4;
	constexpr unsigned SmokeCloudId(int s) { return 0xCE0u + static_cast<unsigned>(s); }
	constexpr unsigned OilSlickId(int s) { return 0xCE9u + static_cast<unsigned>(s); }
	constexpr unsigned SmokeMistHazardId(int s) { return 0xD60u + static_cast<unsigned>(s); }
	constexpr unsigned SmokeMistProjectileId(int s) { return 0xD66u + static_cast<unsigned>(s); }
	inline constexpr unsigned kFrostBurstId = 0xD70, kFearBurstId = 0xD71, kFrenzyBurstId = 0xD72;
	constexpr unsigned BurstLookHazardId(int p) { return 0xD80u + static_cast<unsigned>(p); }
	constexpr unsigned BurstLookProjectileId(int p) { return 0xD90u + static_cast<unsigned>(p); }
	constexpr unsigned HandArtId(int t) { return 0xDA0u + static_cast<unsigned>(t); }
	constexpr unsigned FireSpreadProjectileId(int s) { return 0xD77u + static_cast<unsigned>(s); }
	constexpr int      FireSpreadCount(int s) { return 5 + s; }  // burning patches in the ring around a fire burst

	// Debug smoke bombs: one per candidate smoke effect, to try them out (FirePots.ini [Debug]).
	inline constexpr int kDebugSmokes = 10;
	constexpr unsigned DebugHazardId(int k) { return 0xD10u + static_cast<unsigned>(k); }
	constexpr unsigned DebugExplosionId(int k) { return 0xD20u + static_cast<unsigned>(k); }
	constexpr unsigned DebugProjectileId(int k) { return 0xD30u + static_cast<unsigned>(k); }
	constexpr unsigned DebugItemId(int k) { return 0xD50u + static_cast<unsigned>(k); }

	// --- Numbers (game units; 70 units ~ 1 m) ---
	constexpr float BlastRadius(int a_payload, int a_strength)
	{
		const float r = 140.0f + 70.0f * static_cast<float>(a_strength);
		return a_payload == kConcussion ? r * 1.75f : r;
	}
	constexpr float OilSlickRadius(int a_strength) { return 120.0f + 60.0f * static_cast<float>(a_strength); }
	inline constexpr float kTripwireRadius = 120.0f;  // proximity trigger (ESP)
	constexpr int FragDamage(int s) { return 25 * (s + 1); }
	constexpr int FireDamage(int s) { return 20 * (s + 1); }
	constexpr int FrostDamage(int s) { return 20 * (s + 1); }
	constexpr int FrostSlowPercent(int s) { return 30 + 10 * s; }
	constexpr int FrostSlowSeconds(int s) { return 3 + 2 * s; }
	constexpr int OilSlowPercent(int s) { return 20 + 15 * s; }
	inline constexpr int kOilSlickSeconds = 60;
	constexpr int MindLevel(int s) { return 10 * (s + 1); }       // fear / frenzy: highest level affected
	constexpr int MindSeconds(int s) { return 4 + 6 * (s + 1); }  // fear / frenzy duration

	// Fuse bombs are real physics objects (they bounce and roll); the fuse is lit when thrown.
	constexpr float FuseSeconds(int a_trigger) { return a_trigger == kShortFuse ? 2.0f : a_trigger == kLongFuse ? 4.5f : 0.0f; }
	constexpr bool  IsFuse(int a_trigger) { return a_trigger == kShortFuse || a_trigger == kLongFuse; }
	constexpr bool  IsPlaced(int a_trigger) { return a_trigger == kTripwire; }
	inline constexpr float kTripwireArmSeconds = 3.0f;

	// Any bomb lying in the world (set down, dropped, a fuse bomb still rolling) goes off when struck
	// or caught in another bomb's blast.

	// Payloads in the game. Smoke is out (it only made everyone in it invisible); its IDs stay reserved.
	constexpr bool Available(int a_payload) { return a_payload != kSmoke; }

	inline std::string Name(int a_index)
	{
		return std::string(kStrengthNames[StrengthOf(a_index)]) + " " + std::string(kPayloadNames[PayloadOf(a_index)]) +
		       " Bomb (" + std::string(kTriggerNames[TriggerOf(a_index)]) + ")";
	}

	// "2 m", "3.5 m"
	inline std::string Meters(float a_units)
	{
		char        buf[16];
		const float m = a_units / 70.0f;
		if (m == static_cast<float>(static_cast<int>(m))) {
			std::snprintf(buf, sizeof(buf), "%d m", static_cast<int>(m));
		} else {
			std::snprintf(buf, sizeof(buf), "%.1f m", m);
		}
		return buf;
	}

	// The item description (tools/MakePlugin writes the same text into the plugin).
	inline std::string Describe(int a_trigger, int a_payload, int a_strength)
	{
		const int   s = a_strength;
		const auto  r = Meters(BlastRadius(a_payload, s));
		const auto  n = [](int v) { return std::to_string(v); };
		const char* how = a_trigger == kImpact    ? "Shatters on impact:" :
		                  a_trigger == kShortFuse ? "Bounces and rolls, then 2 seconds after the throw:" :
		                  a_trigger == kLongFuse  ? "Bounces and rolls, then 4.5 seconds after the throw:" :
		                                            "Set it down; 3 seconds later it's armed, and when anyone comes within 1.7 m:";
		std::string what;
		switch (a_payload) {
		case kFrag: what = n(FragDamage(s)) + " damage within " + r + ", and a shove"; break;
		case kFire: what = n(FireDamage(s)) + " fire damage within " + r + ", and the ground keeps burning"; break;
		case kFrost:
			what = n(FrostDamage(s)) + " frost damage within " + r + ", and slowed by " + n(FrostSlowPercent(s)) + "% for " +
			       n(FrostSlowSeconds(s)) + " seconds";
			break;
		case kOil:
			what = "an oil slick (" + Meters(OilSlickRadius(s)) + " radius, " + n(kOilSlickSeconds) + " seconds) that slows by " +
			       n(OilSlowPercent(s)) + "%; a fire bomb sets it ablaze";
			break;
		case kSmoke: what = "a lingering cloud that hides anyone inside until they strike, but chokes their stamina"; break;
		case kFear: what = "a blinding flash; anyone up to level " + n(MindLevel(s)) + " within " + r + " flees for " + n(MindSeconds(s)) + " seconds"; break;
		case kFrenzy:
			what = "anyone up to level " + n(MindLevel(s)) + " within " + r + " attacks whoever is nearest for " + n(MindSeconds(s)) + " seconds";
			break;
		default: what = "a deafening blast that staggers everyone within " + r; break;
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
