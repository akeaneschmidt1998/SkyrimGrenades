#pragma once
// Draws the throw preview with ImGui: the flight arc, the blast ring where it lands and the
// wind-up meter. No game or D3D dependencies, so tests/arc_preview.cpp renders it offline.

#include "ArcState.h"

struct ImDrawList;

namespace ArcDraw
{
	struct Style
	{
		bool  showArc = true;  // arc + ring; the meter has its own switch
		float thickness = 3.0f;
		float opacity = 0.9f;
		bool  showRing = true;
		bool  showMeter = true;
	};

	void Draw(ImDrawList* a_drawList, float a_screenW, float a_screenH, const ArcState::Frame& a_frame, const Style& a_style);
}
