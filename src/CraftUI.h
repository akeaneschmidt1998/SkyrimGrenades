#pragma once
// The bomb-making menu, drawn with Dear ImGui (an enchanting-table style panel: choices on the
// left, the bomb and what it takes on the right). No game or D3D code, so tests/craft_preview.cpp
// renders it offline.

#include "CraftShared.h"

namespace CraftUI
{
	struct State
	{
		int  trigger = 0;
		int  payload = 1;
		int  filling = -1;  // -1 = pick the best one for you
		int  strength = 0;
		int  qty = 1;
		bool firstFrame = true;
	};

	struct Result
	{
		bool                                 close = false;
		std::vector<CraftShared::Request>    requests;
	};

	// Draws one frame. a_scale: 1.0 at 1080p.
	Result Draw(const CraftModel::Model& a_model, State& a_state, float a_screenW, float a_screenH);
}
