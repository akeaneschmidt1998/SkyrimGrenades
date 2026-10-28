#pragma once
// Drinking wine gives the player its empty bottle back, so pot containers come from normal play.
// Only wines: vanilla has no empty bottles for mead, ale or spirits.
// Which drink gives which bottle, in priority order:
//   1. sBottle lines in FirePots.ini ("drink -> empty")
//   2. the built-in list of vanilla and DLC wines and their matching empty bottles
//   3. any other drink using a vanilla wine bottle model: an empty wine bottle's model without the
//      "Empty" (catches modded wines that reuse vanilla bottles)
// Turn it off (bReturnEmptyBottles = 0) if another mod already does this, or you'd get two.

namespace BottleReturn
{
	void Init();  // kDataLoaded: build the table, register for equip (= drink) events
}
