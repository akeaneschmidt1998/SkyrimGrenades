#pragma once
// Drinking wine, mead, ale and the like gives the player the empty bottle back, so pot containers
// come from normal play. Which drink gives which bottle, in priority order:
//   1. sBottle lines in FirePots.ini ("drink -> empty")
//   2. the built-in vanilla list (wines to their matching empty bottles; mead, ale and spirits to
//      an empty wine bottle, since vanilla has no empty mead bottle)
//   3. any other drink whose model is an "...Empty" container's model without the "Empty"
//      (catches modded wines that reuse vanilla bottles)
// Turn it off (bReturnEmptyBottles = 0) if another mod already does this, or you'd get two.

namespace BottleReturn
{
	void Init();  // kDataLoaded: build the table, register for equip (= drink) events
}
