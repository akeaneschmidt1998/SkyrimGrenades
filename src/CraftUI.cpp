// No CommonLib / PCH here (also built into the offline preview).
//
// Laid out like Skyrim's own crafting menus: no window, the world stays visible behind a dark
// gradient on the left; uppercase lists in white and grey, the selected entry lit; thin fading
// rules with a diamond; the item card on the right with corner brackets; key hints on a bar at
// the bottom.
#include "CraftUI.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

namespace
{
	// Skyrim's menu palette: white and greys on black, nothing else.
	constexpr ImU32 Col(float v, float a = 1.0f) { return IM_COL32(static_cast<int>(v * 255), static_cast<int>(v * 255), static_cast<int>(v * 250), static_cast<int>(a * 255)); }
	const ImU32 kWhite = Col(0.96f);
	const ImU32 kText = Col(0.74f);
	const ImU32 kDim = Col(0.46f);
	const ImU32 kFaint = Col(0.30f);
	const ImU32 kLack = IM_COL32(205, 105, 90, 255);  // muted red: not enough of it

	float U = 1.0f;  // 1.0 at 1080p

	std::string Upper(std::string a_s)
	{
		for (auto& c : a_s) {
			c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		}
		return a_s;
	}

	// Text with extra space between the letters (Skyrim's headings are tracked out).
	float SpacedWidth(const std::string& a_text, float a_size, float a_tracking)
	{
		ImFont* f = ImGui::GetFont();
		float   w = 0.0f;
		for (std::size_t i = 0; i < a_text.size(); ++i) {
			w += f->CalcTextSizeA(a_size, FLT_MAX, 0.0f, &a_text[i], &a_text[i] + 1).x + (i + 1 < a_text.size() ? a_tracking : 0.0f);
		}
		return w;
	}

	void SpacedText(ImDrawList* dl, ImVec2 a_pos, const std::string& a_text, float a_size, float a_tracking, ImU32 a_col)
	{
		ImFont* f = ImGui::GetFont();
		for (std::size_t i = 0; i < a_text.size(); ++i) {
			dl->AddText(f, a_size, a_pos, a_col, &a_text[i], &a_text[i] + 1);
			a_pos.x += f->CalcTextSizeA(a_size, FLT_MAX, 0.0f, &a_text[i], &a_text[i] + 1).x + a_tracking;
		}
	}

	ImVec2 TextSize(const std::string& a_text, float a_size)
	{
		return ImGui::GetFont()->CalcTextSizeA(a_size, FLT_MAX, 0.0f, a_text.c_str());
	}

	void Text(ImDrawList* dl, ImVec2 a_pos, const std::string& a_text, float a_size, ImU32 a_col)
	{
		dl->AddText(ImGui::GetFont(), a_size, a_pos, a_col, a_text.c_str());
	}

	// A one-pixel rule that fades out at both ends.
	void Rule(ImDrawList* dl, float x0, float x1, float y, float a_alpha = 0.55f)
	{
		const float mid = (x0 + x1) * 0.5f;
		const float t = (std::max)(1.0f, U);
		dl->AddRectFilledMultiColor({ x0, y }, { mid, y + t }, Col(0.9f, 0.0f), Col(0.9f, a_alpha), Col(0.9f, a_alpha), Col(0.9f, 0.0f));
		dl->AddRectFilledMultiColor({ mid, y }, { x1, y + t }, Col(0.9f, a_alpha), Col(0.9f, 0.0f), Col(0.9f, 0.0f), Col(0.9f, a_alpha));
	}

	void Diamond(ImDrawList* dl, ImVec2 c, float r, ImU32 a_col, bool a_filled = true)
	{
		const ImVec2 a{ c.x, c.y - r }, b{ c.x + r, c.y }, d{ c.x, c.y + r }, e{ c.x - r, c.y };
		if (a_filled) {
			dl->AddQuadFilled(a, b, d, e, a_col);
		} else {
			dl->AddQuad(a, b, d, e, a_col, (std::max)(1.0f, U));
		}
	}

	// Rule with a small diamond in the middle and dots near the ends.
	void Ornament(ImDrawList* dl, float x0, float x1, float y)
	{
		const float mid = (x0 + x1) * 0.5f;
		Rule(dl, x0, mid - 9.0f * U, y, 0.5f);
		Rule(dl, mid + 9.0f * U, x1, y, 0.5f);
		Diamond(dl, { mid, y + 0.5f }, 5.0f * U, Col(0.85f, 0.8f), false);
		Diamond(dl, { mid, y + 0.5f }, 2.0f * U, Col(0.9f, 0.9f));
	}

	// Section heading: small tracked capitals in grey over a fading rule.
	void Heading(ImDrawList* dl, ImVec2 a_pos, float a_width, const std::string& a_text, const std::string& a_right = {})
	{
		const float size = 16.0f * U;
		SpacedText(dl, a_pos, Upper(a_text), size, 2.5f * U, kDim);
		if (!a_right.empty()) {
			const float w = SpacedWidth(Upper(a_right), 13.0f * U, 1.5f * U);
			SpacedText(dl, { a_pos.x + a_width - w, a_pos.y + 2.0f * U }, Upper(a_right), 13.0f * U, 1.5f * U, kFaint);
		}
		Rule(dl, a_pos.x - 10.0f * U, a_pos.x + a_width + 10.0f * U, a_pos.y + size + 6.0f * U, 0.35f);
	}

	// A list entry. The selected one is white with a diamond and a soft bar behind it; the one
	// under the mouse / gamepad focus is lit; ones you can't make are dark grey.
	bool Row(const char* a_id, const std::string& a_name, const std::string& a_note, bool a_selected, bool a_usable, float a_width)
	{
		ImGui::PushID(a_id);
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float  h = 33.0f * U;
		const bool   clicked = ImGui::Selectable("##row", a_selected, 0, { a_width, h });
		if (a_selected && ImGui::IsWindowAppearing()) {
			ImGui::SetItemDefaultFocus();
		}
		const bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
		auto*      dl = ImGui::GetWindowDrawList();
		if (a_selected || hot) {
			const float a = a_selected ? 0.13f : 0.06f;
			dl->AddRectFilledMultiColor(p, { p.x + a_width, p.y + h }, Col(1.0f, a), Col(1.0f, 0.0f), Col(1.0f, 0.0f), Col(1.0f, a));
		}
		if (a_selected) {
			Diamond(dl, { p.x + 9.0f * U, p.y + h * 0.5f }, 4.0f * U, kWhite);
		}
		const float size = 22.0f * U;
		const float ty = p.y + (h - size) * 0.5f - 1.0f * U;
		const ImU32 col = !a_usable ? (a_selected || hot ? kText : kDim) : (a_selected || hot ? kWhite : kText);
		const float nw = a_note.empty() ? 0.0f : TextSize(a_note, 18.0f * U).x;
		dl->PushClipRect(p, { p.x + a_width - nw - 14.0f * U, p.y + h }, true);
		Text(dl, { p.x + 24.0f * U, ty }, Upper(a_name), size, col);
		dl->PopClipRect();
		if (!a_note.empty()) {
			Text(dl, { p.x + a_width - nw - 6.0f * U, p.y + (h - 18.0f * U) * 0.5f }, a_note, 18.0f * U, a_usable ? kDim : kFaint);
		}
		ImGui::PopID();
		return clicked;
	}

	// Word-wrapped, centred lines.
	float CenteredWrapped(ImDrawList* dl, float a_cx, float a_y, float a_width, const std::string& a_text, float a_size, ImU32 a_col)
	{
		std::vector<std::string> words;
		std::string              w;
		for (char c : a_text) {
			if (c == ' ') {
				if (!w.empty()) words.push_back(w);
				w.clear();
			} else {
				w += c;
			}
		}
		if (!w.empty()) words.push_back(w);
		std::string line;
		const float lh = a_size * 1.3f;
		auto flush = [&] {
			if (line.empty()) return;
			Text(dl, { a_cx - TextSize(line, a_size).x * 0.5f, a_y }, line, a_size, a_col);
			a_y += lh;
			line.clear();
		};
		for (const auto& word : words) {
			const std::string next = line.empty() ? word : line + " " + word;
			if (!line.empty() && TextSize(next, a_size).x > a_width) {
				flush();
				line = word;
			} else {
				line = next;
			}
		}
		flush();
		return a_y;
	}

	// A text button in Skyrim's style: thin frame, lit when hovered or focused.
	bool FrameButton(const char* a_id, const std::string& a_label, ImVec2 a_size, float a_textSize, bool a_enabled)
	{
		ImGui::PushID(a_id);
		const ImVec2 p = ImGui::GetCursorScreenPos();
		ImGui::BeginDisabled(!a_enabled);
		const bool clicked = ImGui::InvisibleButton("##b", a_size);
		const bool hot = a_enabled && (ImGui::IsItemHovered() || ImGui::IsItemFocused());
		const bool held = ImGui::IsItemActive();
		ImGui::EndDisabled();
		auto*        dl = ImGui::GetWindowDrawList();
		const ImVec2 q{ p.x + a_size.x, p.y + a_size.y };
		dl->AddRectFilled(p, q, Col(1.0f, held ? 0.22f : hot ? 0.12f : 0.04f));
		dl->AddRect(p, q, a_enabled ? (hot ? Col(0.95f, 0.9f) : Col(0.8f, 0.45f)) : Col(0.5f, 0.25f), 0.0f, 0, (std::max)(1.0f, U));
		const std::string label = Upper(a_label);
		const float       tw = SpacedWidth(label, a_textSize, 2.0f * U);
		SpacedText(dl, { p.x + (a_size.x - tw) * 0.5f, p.y + (a_size.y - a_textSize) * 0.5f - 1.0f * U }, label, a_textSize, 2.0f * U,
			a_enabled ? (hot ? kWhite : kText) : kFaint);
		ImGui::PopID();
		return clicked && a_enabled;
	}

	// Key caps ("E", "A") and what they do, for the bottom bar. Returns the width used (draws
	// nothing when a_dl is null).
	float Hint(ImDrawList* a_dl, ImVec2 a_pos, std::initializer_list<const char*> a_keys, const std::string& a_what)
	{
		const float ks = 15.0f * U, kh = 24.0f * U;
		float       x = a_pos.x;
		for (const char* key : a_keys) {
			const float kw = (std::max)(26.0f * U, TextSize(key, ks).x + 12.0f * U);
			if (a_dl) {
				a_dl->AddRect({ x, a_pos.y }, { x + kw, a_pos.y + kh }, Col(0.85f, 0.7f), 3.0f * U, 0, (std::max)(1.0f, U));
				Text(a_dl, { x + (kw - TextSize(key, ks).x) * 0.5f, a_pos.y + (kh - ks) * 0.5f - 1.0f * U }, key, ks, kWhite);
			}
			x += kw + 5.0f * U;
		}
		const std::string what = Upper(a_what);
		const float       ws = 17.0f * U;
		if (a_dl) {
			SpacedText(a_dl, { x + 5.0f * U, a_pos.y + (kh - ws) * 0.5f - 1.0f * U }, what, ws, 1.5f * U, kText);
		}
		return x + 5.0f * U + SpacedWidth(what, ws, 1.5f * U) - a_pos.x;
	}

	constexpr ImGuiWindowFlags kBare = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
	                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar;

	// Starts a transparent column at an absolute screen rectangle (gamepad / arrows move between
	// columns as if they were one list).
	void BeginColumn(const char* a_id, ImVec2 a_pos, ImVec2 a_size)
	{
		ImGui::SetCursorScreenPos(a_pos);
		ImGui::BeginChild(a_id, a_size, ImGuiChildFlags_NavFlattened, kBare);
	}
}

namespace CraftUI
{
	Result Draw(const CraftModel::Model& m, State& st, float sw, float sh)
	{
		Result out;
		U = sh / 1080.0f;
		// lay out on a 16:9 area centred on wider screens
		const float aw = (std::min)(sw, sh * 16.0f / 9.0f);
		const float ox = (sw - aw) * 0.5f;
		auto        X = [&](float a_x) { return ox + a_x * U; };
		auto        Y = [&](float a_y) { return a_y * U; };

		// payloads that exist
		std::vector<int> payloads;
		for (int p = 0; p < Bombs::kP; ++p) {
			if (Bombs::Available(p)) {
				payloads.push_back(p);
			}
		}
		st.trigger = std::clamp(st.trigger, 0, Bombs::kT - 1);
		st.strength = std::clamp(st.strength, 0, Bombs::kS - 1);
		if (std::find(payloads.begin(), payloads.end(), st.payload) == payloads.end()) {
			st.payload = payloads.empty() ? 0 : payloads.front();
			st.filling = -1;
		}
		const int nFill = static_cast<int>(m.fillings[st.payload].size());
		const int fill = (st.filling >= 0 && st.filling < nFill) ? st.filling : CraftModel::BestFilling(m, st.trigger, st.payload, st.strength);
		const auto res = CraftModel::Resolve(m, st.trigger, st.payload, fill, st.strength);
		const int  bomb = Bombs::Index(st.trigger, st.payload, st.strength);
		st.qty = std::clamp(st.qty, 1, (std::max)(1, res.canMake));

		// --- backdrop: the world dimmed, darker on the left behind the lists, a bar at the bottom ---
		auto* bg = ImGui::GetBackgroundDrawList();
		bg->AddRectFilled({ 0, 0 }, { sw, sh }, IM_COL32(0, 0, 0, 70));
		const float fadeEnd = X(1020.0f);
		bg->AddRectFilled({ 0, 0 }, { X(560.0f), sh }, IM_COL32(0, 0, 0, 200));
		bg->AddRectFilledMultiColor({ X(560.0f), 0 }, { fadeEnd, sh }, IM_COL32(0, 0, 0, 200), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 200));
		if (ox > 0.0f) {
			bg->AddRectFilled({ 0, 0 }, { ox, sh }, IM_COL32(0, 0, 0, 200));
		}
		bg->AddRectFilledMultiColor({ 0, Y(960.0f) }, { sw, sh }, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 220), IM_COL32(0, 0, 0, 220));

		ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_NavCursor, IM_COL32(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_Text, kText);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.0f, 0.0f });
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.0f, 2.0f * U });
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);

		ImGui::SetNextWindowPos({ 0, 0 });
		ImGui::SetNextWindowSize({ sw, sh });
		ImGui::Begin("##bombmaking", nullptr, kBare | ImGuiWindowFlags_NoBringToFrontOnFocus);
		if (st.firstFrame) {
			ImGui::SetWindowFocus();
			st.firstFrame = false;
		}
		auto* dl = ImGui::GetWindowDrawList();

		// --- title ---
		{
			const std::string title = "BOMB-MAKING";
			SpacedText(dl, { X(96.0f), Y(64.0f) }, title, 38.0f * U, 5.0f * U, kWhite);
			Rule(dl, X(70.0f), X(800.0f), Y(116.0f), 0.6f);
			char sub[96];
			std::snprintf(sub, sizeof(sub), "%d EMPTY CONTAINER%s CARRIED", m.containers, m.containers == 1 ? "" : "S");
			SpacedText(dl, { X(98.0f), Y(126.0f) }, sub, 15.0f * U, 2.0f * U, m.containers > 0 ? kDim : kLack);
		}

		// --- column 1: kind, strength ---
		const float c1x = 86.0f, c1w = 270.0f, top = 182.0f, bottom = 930.0f;
		Heading(dl, { X(c1x + 10.0f), Y(top) }, c1w * U - 20.0f * U, "Kind");
		BeginColumn("##kind", { X(c1x), Y(top + 34.0f) }, { c1w * U, Y(Bombs::kT * 35.0f + 10.0f) });
		{
			static constexpr const char* kKind[Bombs::kT] = { "thrown", "thrown", "thrown", "placed" };
			for (int t = 0; t < Bombs::kT; ++t) {
				if (Row(("t" + std::to_string(t)).c_str(), std::string(Bombs::kTriggerNames[t]), kKind[t], st.trigger == t, true, c1w * U)) {
					st.trigger = t;
				}
			}
		}
		ImGui::EndChild();
		const float sTop = top + 34.0f + Bombs::kT * 35.0f + 34.0f;
		Heading(dl, { X(c1x + 10.0f), Y(sTop) }, c1w * U - 20.0f * U, "Strength");
		BeginColumn("##strength", { X(c1x), Y(sTop + 34.0f) }, { c1w * U, Y(3 * 35.0f + 10.0f) });
		{
			static constexpr const char* kMult[Bombs::kS] = { "x1", "x2", "x3" };
			for (int s = 0; s < Bombs::kS; ++s) {
				const auto r = CraftModel::Resolve(m, st.trigger, st.payload, fill, s);
				if (Row(("s" + std::to_string(s)).c_str(), std::string(Bombs::kStrengthNames[s]), kMult[s], st.strength == s, r.canMake > 0, c1w * U)) {
					st.strength = s;
				}
			}
		}
		ImGui::EndChild();

		// --- column 2: payload, filling ---
		const float c2x = 400.0f, c2w = 330.0f;
		Heading(dl, { X(c2x + 10.0f), Y(top) }, c2w * U - 20.0f * U, "Payload", "can make");
		const float payH = static_cast<float>(payloads.size()) * 35.0f + 10.0f;
		BeginColumn("##payload", { X(c2x), Y(top + 34.0f) }, { c2w * U, Y(payH) });
		for (int p : payloads) {
			const int best = CraftModel::Resolve(m, st.trigger, p, CraftModel::BestFilling(m, st.trigger, p, st.strength), st.strength).canMake;
			if (Row(("p" + std::to_string(p)).c_str(), std::string(Bombs::kPayloadNames[p]), best > 0 ? std::to_string(best) : "", st.payload == p, best > 0, c2w * U)) {
				if (st.payload != p) {
					st.filling = -1;
				}
				st.payload = p;
			}
		}
		ImGui::EndChild();
		const float fTop = top + 34.0f + payH + 24.0f;
		Heading(dl, { X(c2x + 10.0f), Y(fTop) }, c2w * U - 20.0f * U, "Filling", "carried");
		BeginColumn("##filling", { X(c2x), Y(fTop + 34.0f) }, { c2w * U, Y(bottom) - Y(fTop + 34.0f) });
		for (int f = 0; f < nFill; ++f) {
			const auto& a = m.fillings[st.payload][f];
			const int   have = m.Have(a.id);
			std::string name = m.Name(a.id);
			if (a.count > 1) {
				name += " x" + std::to_string(a.count);
			}
			if (Row(("f" + std::to_string(f)).c_str(), name, std::to_string(have), fill == f, have >= a.count * (st.strength + 1), c2w * U)) {
				st.filling = f;
			}
		}
		if (nFill == 0) {
			Text(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), "Nothing is set up as a filling for this.", 18.0f * U, kLack);
		}
		ImGui::EndChild();

		// --- item card ---
		const ImVec2 cMin{ X(1090.0f), Y(290.0f) };
		const ImVec2 cMax{ X(1800.0f), Y(840.0f) };
		const float  cw = cMax.x - cMin.x;
		const float  cx = (cMin.x + cMax.x) * 0.5f;
		{
			dl->AddRectFilled(cMin, cMax, IM_COL32(0, 0, 0, 165));
			dl->AddRect(cMin, cMax, Col(0.85f, 0.22f), 0.0f, 0, (std::max)(1.0f, U));
			// corner brackets
			const float L = 22.0f * U, t = (std::max)(1.0f, 1.5f * U);
			const ImU32 bc = Col(0.9f, 0.75f);
			const ImVec2 corners[4] = { cMin, { cMax.x, cMin.y }, cMax, { cMin.x, cMax.y } };
			for (int i = 0; i < 4; ++i) {
				const float sx = (i == 1 || i == 2) ? -1.0f : 1.0f, sy = (i >= 2) ? -1.0f : 1.0f;
				const ImVec2 c = corners[i];
				dl->AddLine(c, { c.x + sx * L, c.y }, bc, t);
				dl->AddLine(c, { c.x, c.y + sy * L }, bc, t);
				Diamond(dl, { c.x + sx * 5.0f * U, c.y + sy * 5.0f * U }, 2.0f * U, bc);
			}

			// name, kind, description
			const std::string name = Upper(m.names[bomb].empty() ? Bombs::Name(bomb) : m.names[bomb]);
			float tsize = 30.0f * U;
			while (TextSize(name, tsize).x > cw - 60.0f * U && tsize > 18.0f * U) {
				tsize -= 1.0f * U;
			}
			Text(dl, { cx - TextSize(name, tsize).x * 0.5f, cMin.y + 28.0f * U }, name, tsize, kWhite);
			char carry[48];
			std::snprintf(carry, sizeof(carry), "YOU CARRY %d", m.carried[bomb]);
			const float cwid = SpacedWidth(carry, 14.0f * U, 2.0f * U);
			SpacedText(dl, { cx - cwid * 0.5f, cMin.y + 70.0f * U }, carry, 14.0f * U, 2.0f * U, kDim);
			Ornament(dl, cMin.x + 40.0f * U, cMax.x - 40.0f * U, cMin.y + 98.0f * U);
			float y = CenteredWrapped(dl, cx, cMin.y + 116.0f * U, cw - 90.0f * U, Bombs::Describe(st.trigger, st.payload, st.strength), 20.0f * U, kText);

			// what one takes
			y += 18.0f * U;
			const float lx = cMin.x + 60.0f * U, rx = cMax.x - 60.0f * U;
			Heading(dl, { lx, y }, rx - lx, "Each one takes", "needed / carried");
			y += 36.0f * U;
			auto need = [&](const std::string& a_name, const std::string& a_hint, int a_need, int a_have) {
				const ImU32 c = a_have >= a_need ? kWhite : kLack;
				Text(dl, { lx, y }, a_name, 20.0f * U, c);
				if (!a_hint.empty()) {
					Text(dl, { lx + TextSize(a_name, 20.0f * U).x + 8.0f * U, y + 2.0f * U }, a_hint, 17.0f * U, kDim);
				}
				char right[32];
				std::snprintf(right, sizeof(right), "%d / %d", a_need, a_have);
				Text(dl, { rx - TextSize(right, 20.0f * U).x, y }, right, 20.0f * U, c);
				y += 28.0f * U;
			};
			need("Empty container", "(bottle, jug or pot)", 1, m.containers);
			for (const auto& n : res.needs) {
				need(n.name, n.alternatives > 0 ? "(or another kind)" : "", n.need, n.have);
			}
		}

		// amount and craft, inside the card at the bottom
		{
			const float btnW = 230.0f * U, btnH = 46.0f * U, small = 40.0f * U;
			const float rowY = cMax.y - 128.0f * U;
			const char* can = res.canMake > 0 ? "YOU CAN MAKE %d" : "YOU CAN'T MAKE ANY YET";
			char        canText[48];
			std::snprintf(canText, sizeof(canText), can, res.canMake);
			const float canW = SpacedWidth(canText, 16.0f * U, 2.0f * U);
			SpacedText(dl, { cx - canW * 0.5f, rowY - 30.0f * U }, canText, 16.0f * U, 2.0f * U, res.canMake > 0 ? kText : kLack);

			BeginColumn("##craft", { cMin.x, rowY }, { cw, cMax.y - rowY - 10.0f * U });
			const bool  any = res.canMake > 0;
			const float rowW = small * 2.0f + 70.0f * U + 10.0f * U + small * 1.6f + 10.0f * U;
			ImGui::SetCursorPosX((cw - rowW) * 0.5f);
			if (FrameButton("minus", "-", { small, small * 0.85f }, 28.0f * U, any && st.qty > 1)) {
				st.qty = (std::max)(1, st.qty - 1);
			}
			ImGui::SameLine(0.0f, 0.0f);
			{
				const ImVec2 p = ImGui::GetCursorScreenPos();
				ImGui::Dummy({ 70.0f * U, small * 0.85f });
				const std::string q = std::to_string(any ? st.qty : 0);
				Text(ImGui::GetWindowDrawList(), { p.x + (70.0f * U - TextSize(q, 24.0f * U).x) * 0.5f, p.y + (small * 0.85f - 24.0f * U) * 0.5f }, q, 24.0f * U,
					any ? kWhite : kFaint);
			}
			ImGui::SameLine(0.0f, 0.0f);
			if (FrameButton("plus", "+", { small, small * 0.85f }, 28.0f * U, any && st.qty < res.canMake)) {
				st.qty = (std::min)(res.canMake, st.qty + 1);
			}
			ImGui::SameLine(0.0f, 10.0f * U);
			if (FrameButton("max", "Max", { small * 1.6f, small * 0.85f }, 15.0f * U, any && st.qty < res.canMake)) {
				st.qty = res.canMake;
			}
			ImGui::Dummy({ 0.0f, 12.0f * U });
			ImGui::SetCursorPosX((cw - btnW) * 0.5f);
			char label[32];
			std::snprintf(label, sizeof(label), st.qty > 1 && any ? "Craft %d" : "Craft", st.qty);
			if (FrameButton("craft", label, { btnW, btnH }, 20.0f * U, any)) {
				out.requests.push_back({ st.trigger, st.payload, fill, st.strength, st.qty });
			}
			ImGui::EndChild();
		}

		// --- bottom bar: key hints ---
		{
			Rule(dl, 0.0f, sw, Y(986.0f), 0.45f);
			const float gapW = 48.0f * U;
			const float w1 = Hint(nullptr, {}, { "E", "A" }, "Select");
			const float w2 = Hint(nullptr, {}, { "Tab", "B" }, "Exit");
			float       x = (sw - w1 - w2 - gapW) * 0.5f;
			x += Hint(dl, { x, Y(1006.0f) }, { "E", "A" }, "Select") + gapW;
			Hint(dl, { x, Y(1006.0f) }, { "Tab", "B" }, "Exit");
		}

		ImGui::End();
		ImGui::PopStyleVar(4);
		ImGui::PopStyleColor(5);

		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_Tab, false) ||
			ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
			out.close = true;
		}
		return out;
	}
}
