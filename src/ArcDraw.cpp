// No CommonLib / PCH here (also built into the offline preview).
#include "ArcDraw.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
	ImU32 Rgba(int r, int g, int b, float a)
	{
		return IM_COL32(r, g, b, static_cast<int>(std::clamp(a, 0.0f, 1.0f) * 255.0f));
	}

	ImVec2 ToPx(const ArcState::ScreenPt& p, float w, float h) { return { p.x * w, p.y * h }; }

	// Strokes a projected polyline, skipping segments with an end behind the camera or wildly off
	// screen. a_alpha(i, n) gives each segment's opacity.
	template <class AlphaFn>
	void Polyline(ImDrawList* dl, const std::vector<ArcState::ScreenPt>& pts, float w, float h, float thickness,
		int r, int g, int b, AlphaFn&& a_alpha)
	{
		const auto n = pts.size();
		for (std::size_t i = 1; i < n; ++i) {
			const auto& p0 = pts[i - 1];
			const auto& p1 = pts[i];
			if (!p0.visible || !p1.visible) {
				continue;
			}
			const auto offscreen = [](const ArcState::ScreenPt& p) { return p.x < -1.0f || p.x > 2.0f || p.y < -1.0f || p.y > 2.0f; };
			if (offscreen(p0) || offscreen(p1)) {
				continue;
			}
			// dark outline first so the arc reads over snow and sky
			const float a = a_alpha(i, n);
			dl->AddLine(ToPx(p0, w, h), ToPx(p1, w, h), Rgba(20, 10, 0, a * 0.6f), thickness + 2.0f);
			dl->AddLine(ToPx(p0, w, h), ToPx(p1, w, h), Rgba(r, g, b, a), thickness);
		}
	}
}

namespace ArcDraw
{
	void Draw(ImDrawList* dl, float w, float h, const ArcState::Frame& f, const Style& s)
	{
		if (!f.active) {
			return;
		}
		const float o = s.opacity;

		// Arc: bright near the hand, slightly dimmer far away; if nothing is hit within the traced
		// time it fades out to say "it's going further than this".
		// arc and ring colours per payload: frag, fire, oil, smoke, fear, frenzy, concussion
		static constexpr int kArc[7][3] = { { 210, 190, 160 }, { 255, 170, 60 }, { 200, 150, 60 }, { 200, 200, 200 },
			{ 255, 245, 170 }, { 230, 90, 140 }, { 150, 210, 255 } };
		static constexpr int kRing[7][3] = { { 180, 150, 110 }, { 255, 110, 30 }, { 170, 110, 30 }, { 160, 160, 160 },
			{ 255, 230, 90 }, { 200, 50, 110 }, { 90, 170, 255 } };
		const int c = std::clamp(f.color, 0, 6);
		const int ar = kArc[c][0], ag = kArc[c][1], ab = kArc[c][2];
		if (s.showArc) Polyline(dl, f.path, w, h, s.thickness, ar, ag, ab, [&](std::size_t i, std::size_t n) {
			const float t = static_cast<float>(i) / static_cast<float>((std::max)(n - 1, std::size_t{ 1 }));
			const float fade = f.landed ? 1.0f - 0.35f * t : 1.0f - t;
			return o * fade;
		});

		// Blast ring
		if (s.showArc && f.landed && s.showRing && f.ring.size() > 2) {
			// red when it hits someone directly; otherwise the pot's colour
			const int rr = f.hitsActor ? 255 : kRing[c][0];
			const int g = f.hitsActor ? 60 : kRing[c][1];
			const int rb = f.hitsActor ? 30 : kRing[c][2];
			Polyline(dl, f.ring, w, h, s.thickness * 0.8f, rr, g, rb, [&](std::size_t, std::size_t) { return o; });
			// landing dot
			const auto& last = f.path.empty() ? ArcState::ScreenPt{} : f.path.back();
			if (last.visible) {
				dl->AddCircleFilled(ToPx(last, w, h), s.thickness * 1.6f, Rgba(rr, g, rb, o));
			}
		}

		// Wind-up meter under the crosshair: a short arc that fills as you charge.
		if (s.showMeter) {
			const ImVec2 c{ w * 0.5f, h * 0.5f };
			const float  radius = h * 0.045f;
			const float  start = 0.75f * 3.14159265f;  // lower-left
			const float  sweep = 1.5f * 3.14159265f;   // to lower-right, over the top
			const int    segs = 40;
			auto ringArc = [&](float frac, ImU32 col, float th) {
				dl->PathClear();
				const int k = (std::max)(1, static_cast<int>(segs * frac));
				for (int i = 0; i <= k; ++i) {
					const float a = start + sweep * frac * static_cast<float>(i) / static_cast<float>(k);
					dl->PathLineTo({ c.x + std::cos(a) * radius, c.y + std::sin(a) * radius });
				}
				dl->PathStroke(col, 0, th);
			};
			ringArc(1.0f, Rgba(0, 0, 0, o * 0.45f), s.thickness + 3.0f);
			if (f.charge > 0.001f) {
				const bool full = f.charge >= 0.999f;
				const ImU32 col = full ? Rgba((ar + 255) / 2, (ag + 255) / 2, (ab + 255) / 2, o) : Rgba(ar, ag, ab, o);
				ringArc(std::clamp(f.charge, 0.0f, 1.0f), col, s.thickness + 1.0f);
			}

			char text[64];
			std::snprintf(text, sizeof(text), "%s x%d", f.potName.c_str(), f.potsLeft);
			const ImVec2 ts = ImGui::CalcTextSize(text);
			const ImVec2 tp{ c.x - ts.x * 0.5f, c.y + radius + 6.0f };
			dl->AddText({ tp.x + 1, tp.y + 1 }, Rgba(0, 0, 0, o), text);
			dl->AddText(tp, Rgba(255, 230, 200, o), text);
		}
	}
}
