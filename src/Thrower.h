#pragma once
// The player's throw:
//   Hold the throw key (G) to wind up: the charge sets the launch speed, and the overlay shows the
//   arc the pot will fly, where it lands and its blast radius. Release to throw; a quick tap or
//   pressing block cancels without using a pot.
//   The pot is a real projectile (FirePots.esp) launched from the throwing hand with the speed and
//   angle the preview showed. It explodes on contact, which is where the fire comes from.
// The first throw is also measured: the engine's real gravity and launch speed replace our
// defaults so the preview matches the flight exactly afterwards (logged as "Flight check").

namespace Thrower
{
	void InstallHooks();   // at plugin load: per-frame update on the player
	void RegisterInput();  // at kInputLoaded
	void RegisterEvents(); // at kDataLoaded: hit events (volatile bombs)
	void Reset();          // new game / load: drop any wind-up in progress
	void GiveDebugBombs(); // after a load / new game: top the debug smoke bombs up to 5 each (if on)
}
