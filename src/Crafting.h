#pragma once
// Pot making, anywhere: the craft key (H) opens three short native message boxes:
//   1. choose a container  - every container, with how many you carry (carried ones first)
//   2. choose a filling    - every filling, with how many you carry and which pot it makes
//   3. how many            - 1 / 5 / all you can make (skipped when you can only make one); if you
//                            can't make any, it says what it takes and what you're missing
// It always opens, so it doubles as the "what can I make pots from" list.
// Each list pages after 8 entries and has Back / Cancel. Items are counted again at the moment
// you confirm, so nothing can go negative.

namespace Crafting
{
	// Game thread (from the player update). Shows why not if it can't open.
	void Open(RE::PlayerCharacter* a_player);
}
