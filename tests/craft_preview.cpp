// Offline preview of the bomb-making menu: runs the real CraftUI::Draw through ImGui with a made-up
// inventory and rasterizes it in software to PPM images. No GPU, game or Windows needed.
//   Usage: craft_preview <out_dir>
#include "CraftUI.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	constexpr int kW = 1920;
	constexpr int kH = 1080;

	struct Tex
	{
		int                        w = 0, h = 0, bpp = 0;
		std::vector<unsigned char> px;
	};
	std::vector<Tex> g_textures;

	void HandleTextureRequests(ImDrawData* dd)
	{
		if (!dd->Textures) return;
		for (ImTextureData* t : *dd->Textures) {
			if (t->Status == ImTextureStatus_WantCreate || t->Status == ImTextureStatus_WantUpdates) {
				int id = t->TexID == ImTextureID_Invalid ? static_cast<int>(g_textures.size()) : static_cast<int>(t->TexID) - 1;
				if (id >= static_cast<int>(g_textures.size())) g_textures.resize(id + 1);
				Tex& dst = g_textures[id];
				dst.w = t->Width; dst.h = t->Height; dst.bpp = t->BytesPerPixel;
				dst.px.assign(t->Pixels, t->Pixels + t->GetSizeInBytes());
				t->SetTexID(static_cast<ImTextureID>(id + 1));
				t->SetStatus(ImTextureStatus_OK);
			} else if (t->Status == ImTextureStatus_WantDestroy) {
				t->SetTexID(ImTextureID_Invalid);
				t->SetStatus(ImTextureStatus_Destroyed);
			}
		}
	}

	struct Image
	{
		std::vector<float> rgb = std::vector<float>(kW * kH * 3);
	};

	void Background(Image& img)
	{
		for (int y = 0; y < kH; ++y)
			for (int x = 0; x < kW; ++x) {
				float* p = &img.rgb[(y * kW + x) * 3];
				const float t = static_cast<float>(y) / kH;
				p[0] = 60 + 40 * t; p[1] = 66 + 30 * t; p[2] = 70 + 20 * t;  // a dim "game" behind
			}
	}

	float Edge(const ImVec2& a, const ImVec2& b, float x, float y) { return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x); }

	void Raster(Image& img, ImDrawData* dd)
	{
		for (int n = 0; n < dd->CmdListsCount; ++n) {
			const ImDrawList* cl = dd->CmdLists[n];
			for (const ImDrawCmd& cmd : cl->CmdBuffer) {
				if (cmd.UserCallback) continue;
				const int texIdx = static_cast<int>(cmd.GetTexID()) - 1;
				const Tex* tex = (texIdx >= 0 && texIdx < static_cast<int>(g_textures.size())) ? &g_textures[texIdx] : nullptr;
				for (unsigned i = 0; i < cmd.ElemCount; i += 3) {
					const ImDrawVert* v[3];
					for (int k = 0; k < 3; ++k) v[k] = &cl->VtxBuffer[cmd.VtxOffset + cl->IdxBuffer[cmd.IdxOffset + i + k]];
					ImVec2 p[3];
					for (int k = 0; k < 3; ++k) p[k] = v[k]->pos;
					const float area = Edge(p[0], p[1], p[2].x, p[2].y);
					if (std::fabs(area) < 1e-6f) continue;
					int x0 = std::max(0, static_cast<int>(std::floor(std::min({ p[0].x, p[1].x, p[2].x }))));
					int x1 = std::min(kW - 1, static_cast<int>(std::ceil(std::max({ p[0].x, p[1].x, p[2].x }))));
					int y0 = std::max(0, static_cast<int>(std::floor(std::min({ p[0].y, p[1].y, p[2].y }))));
					int y1 = std::min(kH - 1, static_cast<int>(std::ceil(std::max({ p[0].y, p[1].y, p[2].y }))));
					for (int y = y0; y <= y1; ++y)
						for (int x = x0; x <= x1; ++x) {
							const float px = x + 0.5f, py = y + 0.5f;
							float w0 = Edge(p[1], p[2], px, py) / area, w1 = Edge(p[2], p[0], px, py) / area, w2 = 1 - w0 - w1;
							if (w0 < 0 || w1 < 0 || w2 < 0) continue;
							float col[4];
							for (int c = 0; c < 4; ++c) {
								const float c0 = ((v[0]->col >> (8 * c)) & 0xFF), c1 = ((v[1]->col >> (8 * c)) & 0xFF), c2 = ((v[2]->col >> (8 * c)) & 0xFF);
								col[c] = (w0 * c0 + w1 * c1 + w2 * c2) / 255.0f;
							}
							if (tex && !tex->px.empty()) {
								const float u = w0 * v[0]->uv.x + w1 * v[1]->uv.x + w2 * v[2]->uv.x;
								const float vv = w0 * v[0]->uv.y + w1 * v[1]->uv.y + w2 * v[2]->uv.y;
								const int tx = std::clamp(static_cast<int>(u * tex->w), 0, tex->w - 1);
								const int ty = std::clamp(static_cast<int>(vv * tex->h), 0, tex->h - 1);
								const unsigned char* t = &tex->px[(ty * tex->w + tx) * tex->bpp];
								if (tex->bpp == 4) { for (int c = 0; c < 4; ++c) col[c] *= t[c] / 255.0f; }
								else col[3] *= t[0] / 255.0f;
							}
							float* d = &img.rgb[(y * kW + x) * 3];
							for (int c = 0; c < 3; ++c) d[c] = d[c] * (1 - col[3]) + col[c] * 255.0f * col[3];
						}
				}
			}
		}
	}

	CraftModel::Model MakeModel()
	{
		CraftModel::Model m;
		std::uint32_t id = 1;
		auto item = [&](const char* a_name, int a_have) {
			m.items[id] = { a_name, a_have };
			return id++;
		};
		const auto fireSalts = item("Fire Salts", 5), paper = item("Paper Roll", 3), charcoal = item("Charcoal", 4);
		const auto linen = item("Linen Wrap", 0), leather = item("Leather Strips", 6), gear = item("Dwarven Gear", 1);
		m.containers = 4;
		m.trigger[Bombs::kImpact] = { { { { fireSalts, 1 } } } };
		m.trigger[Bombs::kShortFuse] = { { { { paper, 1 } } }, { { { charcoal, 1 } } } };
		m.trigger[Bombs::kLongFuse] = { { { { paper, 2 } } }, { { { charcoal, 1 } } } };
		m.trigger[Bombs::kTripwire] = { { { { linen, 1 }, { leather, 1 } } }, { { { fireSalts, 1 }, { gear, 1 } } } };
		m.fillings[Bombs::kFrag] = { { item("Nails", 12), 1 }, { item("Quarried Stone", 0), 1 } };
		m.fillings[Bombs::kFire] = { { fireSalts, 1 }, { item("Charred Skeever Hide", 3), 2 }, { item("Spawn Ash", 0), 2 } };
		m.fillings[Bombs::kFrost] = { { item("Frost Salts", 2), 1 }, { item("Ice Wraith Teeth", 0), 1 }, { item("Frost Mirriam", 1), 2 } };
		m.fillings[Bombs::kOil] = { { item("Dwarven Oil", 0), 1 }, { item("Troll Fat", 2), 1 } };
		m.fillings[Bombs::kSmoke] = { { charcoal, 1 }, { item("Tundra Cotton", 1), 2 } };
		m.fillings[Bombs::kFear] = { { item("Ectoplasm", 0), 1 }, { item("Glow Dust", 0), 1 } };
		m.fillings[Bombs::kFrenzy] = { { item("Hagraven Feathers", 1), 1 } };
		m.fillings[Bombs::kConcussion] = { { item("Void Salts", 0), 1 }, { item("Bone Meal", 4), 2 } };
		for (int i = 0; i < Bombs::kCount; ++i) {
			m.names[i] = Bombs::Name(i);
		}
		m.carried[Bombs::Index(Bombs::kShortFuse, Bombs::kFire, Bombs::kModerate)] = 2;
		return m;
	}

	void RenderCase(const std::string& outDir, const char* name, const CraftModel::Model& m, CraftUI::State st)
	{
		auto& io = ImGui::GetIO();
		io.DisplaySize = { static_cast<float>(kW), static_cast<float>(kH) };
		io.MousePos = { kW * 0.62f, kH * 0.30f };
		for (int frame = 0; frame < 3; ++frame) {  // a few frames so layout settles
			ImGui::NewFrame();
			CraftUI::Draw(m, st, kW, kH);
			ImGui::Render();
			HandleTextureRequests(ImGui::GetDrawData());
		}
		Image img;
		Background(img);
		Raster(img, ImGui::GetDrawData());
		FILE* f = std::fopen((outDir + "/" + name + ".ppm").c_str(), "wb");
		std::fprintf(f, "P6\n%d %d\n255\n", kW, kH);
		for (float v : img.rgb) std::fputc(static_cast<unsigned char>(std::clamp(v, 0.0f, 255.0f)), f);
		std::fclose(f);
		std::printf("wrote %s\n", name);
	}
}

int main(int argc, char** argv)
{
	const std::string outDir = argc > 1 ? argv[1] : ".";
	ImGui::CreateContext();
	auto& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
	io.DeltaTime = 1.0f / 60.0f;
	ImFontConfig cfg;
	cfg.SizePixels = 24.0f;
	io.Fonts->AddFontDefaultVector(&cfg);

	const auto m = MakeModel();
	CraftUI::State a;
	a.trigger = Bombs::kShortFuse; a.payload = Bombs::kFire; a.strength = Bombs::kModerate;
	RenderCase(outDir, "1_fire_shortfuse", m, a);
	CraftUI::State b;
	b.trigger = Bombs::kTripwire; b.payload = Bombs::kFrag; b.strength = Bombs::kMild;
	RenderCase(outDir, "2_tripwire_frag", m, b);
	CraftUI::State c;
	c.trigger = Bombs::kImpact; c.payload = Bombs::kFear; c.strength = Bombs::kMajor;
	RenderCase(outDir, "3_cant_make", m, c);
	ImGui::DestroyContext();
	return 0;
}
