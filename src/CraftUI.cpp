// No CommonLib / PCH here (also built into the offline preview).
#include "CraftUI.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace
{
	// Enchanting-table palette: dark wood and parchment, gold accents
	const ImVec4 kBg{ 0.06f, 0.05f, 0.04f, 0.94f };
	const ImVec4 kPanel{ 0.11f, 0.09f, 0.07f, 0.90f };
	const ImVec4 kGold{ 0.78f, 0.65f, 0.40f, 1.0f };
	const ImVec4 kCream{ 0.92f, 0.88f, 0.79f, 1.0f };
	const ImVec4 kDim{ 0.55f, 0.51f, 0.45f, 1.0f };
	const ImVec4 kRed{ 0.85f, 0.38f, 0.30f, 1.0f };
	const ImVec4 kGreen{ 0.58f, 0.80f, 0.48f, 1.0f };
	const ImVec4 kSel{ 0.45f, 0.36f, 0.20f, 0.55f };
	const ImVec4 kHover{ 0.45f, 0.36f, 0.20f, 0.30f };

	float g_unit = 1.0f;  // 1.0 at 1080p

	void Header(const char* a_text, const char* a_right = nullptr)
	{
		ImGui::PushFont(nullptr, 17.0f * g_unit);
		if (a_right) {
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const float  w = ImGui::GetContentRegionAvail().x;
			ImGui::GetWindowDrawList()->AddText({ p.x + w - ImGui::CalcTextSize(a_right).x - 8.0f * g_unit, p.y },
				ImGui::GetColorU32(kDim), a_right);
		}
		ImGui::PushStyleColor(ImGuiCol_Text, kGold);
		ImGui::TextUnformatted(a_text);
		ImGui::PopStyleColor();
		ImGui::PopFont();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		ImGui::GetWindowDrawList()->AddLine({ p.x, p.y - 2.0f * g_unit }, { p.x + ImGui::GetContentRegionAvail().x, p.y - 2.0f * g_unit },
			ImGui::GetColorU32(ImVec4(kGold.x, kGold.y, kGold.z, 0.45f)), 1.0f * g_unit);
		ImGui::Dummy({ 0.0f, 4.0f * g_unit });
	}

	// A list row: name on the left, a note right-aligned; dimmed when not usable.
	bool Row(const char* a_id, const std::string& a_name, const std::string& a_note, bool a_selected, bool a_usable)
	{
		ImGui::PushID(a_id);
		const ImVec2 start = ImGui::GetCursorScreenPos();
		const float  w = ImGui::GetContentRegionAvail().x;
		const bool   clicked = ImGui::Selectable("##row", a_selected, 0, { w, ImGui::GetTextLineHeight() + 6.0f * g_unit });
		if (a_selected && ImGui::IsWindowAppearing()) {
			ImGui::SetItemDefaultFocus();
		}
		auto*       dl = ImGui::GetWindowDrawList();
		const float y = start.y + 3.0f * g_unit;
		const float nw = a_note.empty() ? 0.0f : ImGui::CalcTextSize(a_note.c_str()).x;
		const float nameMax = start.x + w - nw - 16.0f * g_unit;
		dl->PushClipRect({ start.x, start.y }, { nameMax, start.y + 100.0f * g_unit }, true);
		dl->AddText({ start.x + 8.0f * g_unit, y }, ImGui::GetColorU32(a_usable ? kCream : kDim), a_name.c_str());
		dl->PopClipRect();
		if (!a_note.empty()) {
			dl->AddText({ start.x + w - nw - 8.0f * g_unit, y }, ImGui::GetColorU32(a_usable ? kGold : kDim), a_note.c_str());
		}
		ImGui::PopID();
		return clicked;
	}

	std::string Count(int a_n) { return a_n > 0 ? std::to_string(a_n) : std::string(); }
}

namespace CraftUI
{
	Result Draw(const CraftModel::Model& m, State& st, float sw, float sh)
	{
		Result out;
		g_unit = sh / 1080.0f;
		const float u = g_unit;

		st.trigger = std::clamp(st.trigger, 0, Bombs::kT - 1);
		st.payload = std::clamp(st.payload, 0, Bombs::kP - 1);
		st.strength = std::clamp(st.strength, 0, Bombs::kS - 1);
		const int nFill = static_cast<int>(m.fillings[st.payload].size());
		const int fill = (st.filling >= 0 && st.filling < nFill) ? st.filling : CraftModel::BestFilling(m, st.trigger, st.payload, st.strength);
		const auto res = CraftModel::Resolve(m, st.trigger, st.payload, fill, st.strength);
		const int bomb = Bombs::Index(st.trigger, st.payload, st.strength);
		st.qty = std::clamp(st.qty, 1, (std::max)(1, res.canMake));

		// --- window ---
		const ImVec2 size{ (std::min)(1180.0f * u, sw * 0.9f), (std::min)(700.0f * u, sh * 0.86f) };
		ImGui::SetNextWindowPos({ sw * 0.5f, sh * 0.5f }, ImGuiCond_Always, { 0.5f, 0.5f });
		ImGui::SetNextWindowSize(size, ImGuiCond_Always);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, kBg);
		ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanel);
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(kGold.x, kGold.y, kGold.z, 0.6f));
		ImGui::PushStyleColor(ImGuiCol_Text, kCream);
		ImGui::PushStyleColor(ImGuiCol_Header, kSel);
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, kHover);
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, kSel);
		ImGui::PushStyleColor(ImGuiCol_NavCursor, kGold);
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.24f, 0.14f, 0.85f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.36f, 0.20f, 0.95f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.58f, 0.46f, 0.26f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f * u);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 22.0f * u, 18.0f * u });
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 8.0f * u, 3.0f * u });
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f * u);
		ImGui::PushFont(nullptr, 19.0f * u);

		ImGui::Begin("##bombmaking", nullptr,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
				ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
		if (st.firstFrame) {
			ImGui::SetWindowFocus();
			st.firstFrame = false;
		}

		// title
		{
			ImGui::PushFont(nullptr, 28.0f * u);
			ImGui::PushStyleColor(ImGuiCol_Text, kGold);
			const char* title = "BOMB-MAKING";
			ImGui::SetCursorPosX((size.x - ImGui::CalcTextSize(title).x) * 0.5f);
			ImGui::TextUnformatted(title);
			ImGui::PopStyleColor();
			ImGui::PopFont();
			char sub[96];
			std::snprintf(sub, sizeof(sub), "Empty containers carried: %d", m.containers);
			ImGui::PushStyleColor(ImGuiCol_Text, m.containers > 0 ? kDim : kRed);
			ImGui::SetCursorPosX((size.x - ImGui::CalcTextSize(sub).x) * 0.5f);
			ImGui::TextUnformatted(sub);
			ImGui::PopStyleColor();
			ImGui::Dummy({ 0.0f, 6.0f * u });
		}

		const float footer = ImGui::GetTextLineHeightWithSpacing() + 10.0f * u;
		const float bodyH = ImGui::GetContentRegionAvail().y - footer;
		const float gap = 14.0f * u;
		const float col1 = size.x * 0.20f, col2 = size.x * 0.29f;

		// --- column 1: kind + strength ---
		ImGui::BeginChild("##c1", { col1, bodyH }, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
		Header("KIND");
		static constexpr const char* kKind[Bombs::kT] = { "thrown", "thrown", "thrown", "placed" };
		for (int t = 0; t < Bombs::kT; ++t) {
			if (Row(std::to_string(t).c_str(), std::string(Bombs::kTriggerNames[t]), kKind[t], st.trigger == t, true)) {
				st.trigger = t;
			}
		}
		ImGui::Dummy({ 0.0f, 10.0f * u });
		Header("STRENGTH");
		static constexpr const char* kMult[Bombs::kS] = { "1x", "2x", "3x" };
		for (int s = 0; s < Bombs::kS; ++s) {
			const auto r = CraftModel::Resolve(m, st.trigger, st.payload, fill, s);
			if (Row(("s" + std::to_string(s)).c_str(), std::string(Bombs::kStrengthNames[s]), kMult[s], st.strength == s, r.canMake > 0)) {
				st.strength = s;
			}
		}
		ImGui::EndChild();

		// --- column 2: payload + filling ---
		ImGui::SameLine(0.0f, gap);
		ImGui::BeginChild("##c2", { col2, bodyH }, ImGuiChildFlags_AlwaysUseWindowPadding);
		Header("PAYLOAD", "can make");
		for (int p = 0; p < Bombs::kP; ++p) {
			const int best = CraftModel::Resolve(m, st.trigger, p, CraftModel::BestFilling(m, st.trigger, p, st.strength), st.strength).canMake;
			if (Row(("p" + std::to_string(p)).c_str(), std::string(Bombs::kPayloadNames[p]), Count(best), st.payload == p, best > 0)) {
				if (st.payload != p) {
					st.filling = -1;
				}
				st.payload = p;
			}
		}
		ImGui::Dummy({ 0.0f, 10.0f * u });
		Header("FILLING", "carried");
		for (int f = 0; f < nFill; ++f) {
			const auto& a = m.fillings[st.payload][f];
			const int   have = m.Have(a.id);
			std::string name = m.Name(a.id);
			if (a.count > 1) {
				name += " x" + std::to_string(a.count);
			}
			if (Row(("f" + std::to_string(f)).c_str(), name, std::to_string(have), fill == f, have >= a.count * (st.strength + 1))) {
				st.filling = f;
			}
		}
		ImGui::EndChild();

		// --- column 3: the bomb ---
		ImGui::SameLine(0.0f, gap);
		ImGui::BeginChild("##c3", { 0.0f, bodyH }, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
		{
			ImGui::PushFont(nullptr, 25.0f * u);
			ImGui::TextUnformatted(m.names[bomb].empty() ? Bombs::Name(bomb).c_str() : m.names[bomb].c_str());
			ImGui::PopFont();
			ImGui::PushStyleColor(ImGuiCol_Text, kDim);
			ImGui::Text("You carry %d", m.carried[bomb]);
			ImGui::PopStyleColor();
			ImGui::Dummy({ 0.0f, 4.0f * u });
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextUnformatted(Bombs::Describe(st.trigger, st.payload, st.strength).c_str());
			ImGui::PopTextWrapPos();
			ImGui::Dummy({ 0.0f, 10.0f * u });

			Header("EACH ONE TAKES");
			auto need = [&](const std::string& a_name, int a_need, int a_have, const char* a_hint) {
				const ImVec2 p = ImGui::GetCursorScreenPos();
				const float  w = ImGui::GetContentRegionAvail().x;
				ImGui::TextUnformatted(a_name.c_str());
				if (a_hint && *a_hint) {
					ImGui::SameLine();
					ImGui::PushStyleColor(ImGuiCol_Text, kDim);
					ImGui::TextUnformatted(a_hint);
					ImGui::PopStyleColor();
				}
				char right[32];
				std::snprintf(right, sizeof(right), "%d  /  %d", a_need, a_have);
				const float rw = ImGui::CalcTextSize(right).x;
				ImGui::GetWindowDrawList()->AddText({ p.x + w - rw, p.y }, ImGui::GetColorU32(a_have >= a_need ? kGreen : kRed), right);
			};
			need("Empty container", 1, m.containers, "(bottle, jug or pot)");
			for (const auto& n : res.needs) {
				need(n.name, n.need, n.have, n.alternatives > 0 ? "(or another kind)" : "");
			}
			if (fill < 0 || fill >= nFill) {
				ImGui::PushStyleColor(ImGuiCol_Text, kRed);
				ImGui::TextUnformatted("No filling is set up for this payload.");
				ImGui::PopStyleColor();
			}
			ImGui::PushStyleColor(ImGuiCol_Text, kDim);
			ImGui::TextUnformatted("needed  /  carried");
			ImGui::PopStyleColor();

			// bottom: amount + craft
			const float btnH = 46.0f * u;
			const float rowH = ImGui::GetFrameHeight();
			ImGui::SetCursorPosY(ImGui::GetWindowHeight() - btnH - rowH - 26.0f * u);
			ImGui::PushStyleColor(ImGuiCol_Text, res.canMake > 0 ? kGold : kRed);
			ImGui::Text(res.canMake > 0 ? "You can make %d" : "You can't make any yet", res.canMake);
			ImGui::PopStyleColor();
			ImGui::SameLine();
			const float bw = 44.0f * u;
			ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 3.0f * bw - 70.0f * u - 22.0f * u);
			ImGui::BeginDisabled(res.canMake <= 0);
			if (ImGui::Button("-", { bw, 0.0f })) {
				st.qty = (std::max)(1, st.qty - 1);
			}
			ImGui::SameLine();
			char q[16];
			std::snprintf(q, sizeof(q), "%d", st.qty);
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (40.0f * u - ImGui::CalcTextSize(q).x) * 0.5f);
			ImGui::TextUnformatted(q);
			ImGui::SameLine(0.0f, (40.0f * u - ImGui::CalcTextSize(q).x) * 0.5f + 8.0f * u);
			if (ImGui::Button("+", { bw, 0.0f })) {
				st.qty = (std::min)(res.canMake, st.qty + 1);
			}
			ImGui::SameLine();
			if (ImGui::Button("Max", { bw * 1.3f, 0.0f })) {
				st.qty = res.canMake;
			}
			ImGui::Dummy({ 0.0f, 6.0f * u });
			ImGui::PushFont(nullptr, 22.0f * u);
			char label[64];
			std::snprintf(label, sizeof(label), st.qty > 1 ? "CRAFT %d" : "CRAFT", st.qty);
			if (ImGui::Button(label, { -1.0f, btnH })) {
				out.requests.push_back({ st.trigger, st.payload, fill, st.strength, st.qty });
			}
			ImGui::PopFont();
			ImGui::EndDisabled();
		}
		ImGui::EndChild();

		// footer
		ImGui::PushStyleColor(ImGuiCol_Text, kDim);
		const char* hint = "Mouse, arrows or d-pad to choose    Enter / A to select    Esc / Tab / B to close";
		ImGui::SetCursorPosX((size.x - ImGui::CalcTextSize(hint).x) * 0.5f);
		ImGui::TextUnformatted(hint);
		ImGui::PopStyleColor();

		ImGui::End();
		ImGui::PopFont();
		ImGui::PopStyleVar(5);
		ImGui::PopStyleColor(12);

		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_Tab, false) ||
			ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
			out.close = true;
		}
		return out;
	}
}
