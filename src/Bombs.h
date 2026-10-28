#pragma once
// The bomb catalogue: trigger x payload x strength, numbered exactly as tools/MakePlugin does.
// Plain std only.

#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace Bombs
{
	enum Trigger : int { kImpact, kShortFuse, kLongFuse, kTripwire, kTriggers };
	enum Payload : int { kFrag, kFire, kOil, kSmoke, kFear, kFrenzy, kConcussion, kPayloads };
	enum Strength : int { kMild, kModerate, kMajor, kStrengths };
	inline constexpr int kCount = static_cast<int>(kTriggers) * static_cast<int>(kPayloads) * static_cast<int>(kStrengths);  // 84

	inline constexpr std::array<std::string_view, kTriggers> kTriggerNames{ "Impact", "Short Fuse", "Long Fuse", "Tripwire" };
	inline constexpr std::array<std::string_view, kPayloads> kPayloadNames{ "Frag", "Fire", "Oil", "Smoke", "Fear", "Frenzy", "Concussion" };
	inline constexpr std::array<std::string_view, kStrengths> kStrengthNames{ "Mild", "Moderate", "Major" };

	constexpr int kP = kPayloads, kS = kStrengths;
	constexpr int Index(int a_trigger, int a_payload, int a_strength) { return (a_trigger * kP + a_payload) * kS + a_strength; }
	constexpr int TriggerOf(int a_index) { return a_index / (kP * kS); }
	constexpr int PayloadOf(int a_index) { return (a_index / kS) % kP; }
	constexpr int StrengthOf(int a_index) { return a_index % kS; }

	// Local form IDs in FirePots.esp
	constexpr unsigned ItemId(int a_index) { return 0x900u + static_cast<unsigned>(a_index); }
	constexpr unsigned ProjectileId(int a_index) { return 0xA00u + static_cast<unsigned>(a_index); }
	constexpr unsigned ExplosionId(int a_payload, int a_strength) { return 0xA60u + static_cast<unsigned>(a_payload * kS + a_strength); }
	constexpr unsigned SmokeCloudId(int a_strength) { return 0xAA0u + static_cast<unsigned>(a_strength); }
	constexpr unsigned OilSlickId(int a_strength) { return 0xAB0u + static_cast<unsigned>(a_strength); }
	inline constexpr unsigned kOilFireProjectileId = 0xAC6;

	// Blast radius per strength (the ring the preview draws); concussion is 1.75x wider.
	constexpr float BlastRadius(int a_payload, int a_strength)
	{
		const float r = 140.0f + 70.0f * static_cast<float>(a_strength);
		return a_payload == kConcussion ? r * 1.75f : r;
	}
	constexpr float OilSlickRadius(int a_strength) { return 120.0f + 60.0f * static_cast<float>(a_strength); }
	inline constexpr float kTripwireRadius = 120.0f;  // proximity trigger (ESP)

	inline std::string Name(int a_index)
	{
		return std::string(kStrengthNames[StrengthOf(a_index)]) + " " + std::string(kPayloadNames[PayloadOf(a_index)]) +
		       " Bomb (" + std::string(kTriggerNames[TriggerOf(a_index)]) + ")";
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
