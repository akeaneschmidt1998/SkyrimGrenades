#pragma once

// Re-reads the settings whenever the Journal menu closes, so MCM changes apply as soon as you
// leave the menu (MCM Helper saves them to Data/MCM/Settings/FirePots.ini).
namespace McmReload
{
	void Register();
}
