#pragma once
// The bomb-making menu, game side. The craft key (H) opens an enchanting-table style panel
// (CraftUI.cpp): pick kind, strength, payload and filling; it shows what one bomb takes, what you
// carry and how many you can make; choose an amount and Craft. While it's up the game is paused
// and its controls are off; mouse, keyboard and gamepad all drive the menu. Esc, Tab, B or the
// craft key close it. An empty container (bottle, jug, pot) is used per bomb, picked for you.

namespace Crafting
{
	void Register();  // kInputLoaded: input listener + the overlay's per-frame callback

	// Game thread (from the player update). Shows why not if it can't open.
	void Open(RE::PlayerCharacter* a_player);
	[[nodiscard]] bool IsOpen();
	void ForceClose();  // loading a save etc.
}
