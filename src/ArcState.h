#pragma once
// What the overlay draws this frame. Written on the game thread (Thrower), read in the
// swap-chain Present hook (OverlayD3D). Plain std only: OverlayD3D.cpp can't include CommonLib.

#include <mutex>
#include <string>
#include <vector>

namespace ArcState
{
	// A point already projected to the screen: x, y in 0..1 from the top-left; visible = in front
	// of the camera (points behind it break the line).
	struct ScreenPt
	{
		float x = 0.0f;
		float y = 0.0f;
		bool  visible = false;
	};

	struct Frame
	{
		bool                  active = false;  // winding up right now
		float                 charge = 0.0f;   // 0..1
		float                 speed = 0.0f;    // units/s the throw would leave with
		bool                  landed = false;  // the arc hits something within the traced time
		bool                  hitsActor = false;
		std::vector<ScreenPt> path;
		std::vector<ScreenPt> ring;  // blast radius at the landing point
		int                   potsLeft = 0;
		std::string           potName = "Fire Pot";
		int                   color = 0;  // 0 fire (orange), 1 frost (blue), 2 smoke (grey), 3 thunder (yellow)
	};

	inline std::mutex g_lock;
	inline Frame      g_frame;

	inline void Publish(Frame&& a_frame)
	{
		std::scoped_lock l(g_lock);
		g_frame = std::move(a_frame);
	}

	inline void Clear()
	{
		std::scoped_lock l(g_lock);
		g_frame.active = false;
		g_frame.path.clear();
		g_frame.ring.clear();
	}

	inline Frame Snapshot()
	{
		std::scoped_lock l(g_lock);
		return g_frame;
	}
}
