#pragma once
// Puts the pots into vanilla leveled lists at load (no ESP edits, so no conflicts with other
// mods that touch the same lists): merchants stock them and bandits/Forsworn carry them.
// Lists, counts and levels come from [Loot] in FirePots.ini (sLoot lines); built-in defaults
// otherwise.

namespace LeveledLists
{
	void Inject();  // kDataLoaded, after Forms::Load
}
