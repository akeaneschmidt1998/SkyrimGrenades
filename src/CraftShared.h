#pragma once
// Between the game side of the bomb-making menu (Crafting.cpp: opens it, pauses the game, feeds
// it input and carries out what you craft) and its drawing (OverlayD3D.cpp + CraftUI.cpp, in the
// swap chain's Present). Plain std only: the overlay can't include CommonLib.

#include "CraftModel.h"

#include <atomic>
#include <mutex>
#include <vector>

namespace CraftShared
{
	struct InputEvent
	{
		enum class Kind
		{
			kMouseDelta,   // a = dx, b = dy (raw mouse units)
			kMouseButton,  // key = 0 left / 1 right / 2 middle, down
			kWheel,        // a = notches (+ = up)
			kKey,          // key = ImGuiKey, down
		};
		Kind  kind = Kind::kKey;
		float a = 0.0f;
		float b = 0.0f;
		int   key = 0;
		bool  down = false;
	};

	struct Request
	{
		int trigger = 0;
		int payload = 0;
		int filling = 0;
		int strength = 0;
		int qty = 1;
	};

	inline std::mutex                lock;
	inline std::atomic<bool>         open{ false };
	inline CraftModel::Model         model;         // guarded by lock
	inline std::vector<InputEvent>   events;        // guarded by lock
	inline std::vector<Request>      requests;      // guarded by lock
	inline std::atomic<bool>         closeRequested{ false };
	inline std::atomic<bool>         justOpened{ false };  // the drawing resets its cursor and focus
	inline float                     mouseSensitivity = 1.0f;

	// Set by the game side; called by the overlay once per frame while the menu is open (so the
	// game side can act even though the game is paused and nothing else is updating).
	inline void (*onFrame)() = nullptr;
}
