#pragma once
// Pot making, anywhere: the craft key (H) opens three short native message boxes:
//   1. choose a container  - only the empty bottles / jugs / pots you carry, with counts
//   2. choose a filling    - only fillings you carry enough of, each saying which pot it makes
//   3. how many            - 1 / 5 / all you can make (skipped when you can only make one)
// Each list pages after 8 entries and has Back / Cancel. Items are counted again at the moment
// you confirm, so nothing can go negative.

namespace Crafting
{
	// Game thread (from the player update). Shows why not if it can't open.
	void Open(RE::PlayerCharacter* a_player);
}
