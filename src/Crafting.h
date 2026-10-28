#pragma once
// Bomb making, anywhere: the craft key (H) opens a few short native message boxes:
//   1. what kind      - thrown (impact / short fuse / long fuse) or tripwire, with what each takes
//   2. what payload   - frag, fire, oil, smoke, fear, frenzy, concussion (with how many you could make)
//   3. what filling   - only when you carry more than one usable filling for that payload
//   4. how strong     - mild / moderate (2x everything) / major (3x), with what each takes
//   5. how many       - 1 / 5 / all (skipped when you can make one); if you can make none, it
//                       says what it takes and what you have
// An empty container (bottle, jug, pot) is used per bomb, picked automatically.
// It always opens, so it doubles as the "what can I make" list. Lists page after 8 entries and
// have Back / Cancel. Items are counted again when you confirm, so nothing can go negative.

namespace Crafting
{
	// Game thread (from the player update). Shows why not if it can't open.
	void Open(RE::PlayerCharacter* a_player);
}
