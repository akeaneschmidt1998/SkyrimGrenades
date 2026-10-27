// Offline preview of the throw overlay: traces real arcs with Ballistics.h, projects them through
// a simple pinhole camera, runs the real ArcDraw::Draw through ImGui and rasterizes the draw
// lists in software to PPM images. No GPU, game or Windows needed.
//   Usage: arc_preview <out_dir>
#include "ArcDraw.h"
#include "Ballistics.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace Ballistics;

namespace
{
	constexpr int kW = 960;
	constexpr int kH = 540;

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

	// --- A tiny scene: camera, ground, optional wall and target ----------------------------
	struct Camera
	{
		Vec3  pos{ 0.0f, 0.0f, 120.0f };
		float pitchDown = 0.12f;  // radians
		float vfov = 60.0f * kPi / 180.0f;

		// world -> camera axes (right, forward, up) for a camera facing +Y pitched down
		Vec3 Forward() const { return { 0.0f, std::cos(pitchDown), -std::sin(pitchDown) }; }
		Vec3 Up() const { return { 0.0f, std::sin(pitchDown), std::cos(pitchDown) }; }
		Vec3 Right() const { return { 1.0f, 0.0f, 0.0f }; }

		ArcState::ScreenPt Project(const Vec3& p) const
		{
			const Vec3  d = p - pos;
			const float zc = d.Dot(Forward());
			if (zc < 1.0f) {
				return {};
			}
			const float f = 0.5f / std::tan(vfov * 0.5f);  // in units of screen height
			const float aspect = static_cast<float>(kW) / kH;
			return { 0.5f + d.Dot(Right()) / zc * f / aspect, 0.5f - d.Dot(Up()) / zc * f, true };
		}
	};

	struct Scene
	{
		float wallY = 0.0f;     // 0 = no wall; else a wall facing the camera at this Y
		float wallTop = 400.0f;
		Vec3  target{};         // an "actor" capsule to hit (radius 40, height 130); y 0 = none
	};

	bool SceneRay(const Scene& s, const Vec3& a, const Vec3& b, float& frac, Vec3& normal, bool& actor)
	{
		float best = 2.0f;
		// ground
		if (a.z > 0.0f && b.z <= 0.0f) {
			best = a.z / (a.z - b.z);
			normal = { 0, 0, 1 };
			actor = false;
		}
		// wall
		if (s.wallY > 0.0f && a.y < s.wallY && b.y >= s.wallY) {
			const float t = (s.wallY - a.y) / (b.y - a.y);
			const Vec3  p = a + (b - a) * t;
			if (t < best && p.z <= s.wallTop) {
				best = t;
				normal = { 0, -1, 0 };
				actor = false;
			}
		}
		// actor: vertical capsule approximated as a cylinder, sampled along the segment
		if (s.target.y > 0.0f) {
			for (int i = 0; i <= 20; ++i) {
				const float t = i / 20.0f;
				const Vec3  p = a + (b - a) * t;
				const float dx = p.x - s.target.x, dy = p.y - s.target.y;
				if (dx * dx + dy * dy < 40.0f * 40.0f && p.z >= s.target.z && p.z <= s.target.z + 130.0f && t < best) {
					best = t;
					normal = Vec3{ dx, dy, 0 }.Normalized();
					actor = true;
					break;
				}
			}
		}
		if (best <= 1.0f) {
			frac = best;
			return true;
		}
		return false;
	}

	ArcState::Frame MakeFrame(const Camera& cam, const Scene& scene, float charge, int pots, float blastRadius)
	{
		const float minSpeed = 700.0f, maxSpeed = 1700.0f, loft = 12.0f * kPi / 180.0f;
		const Vec3  hand = cam.pos + cam.Right() * -22.0f + cam.Forward() * 25.0f + Vec3{ 0, 0, -30.0f };

		Launch launch;
		launch.origin = hand;
		launch.velocity = Loft(cam.Forward(), loft) * ChargeToSpeed(charge, minSpeed, maxSpeed);
		launch.gravity = kDefaultGravityUnits;

		bool hitActor = false;
		auto arc = Trace(launch, 1.0f / 30.0f, 4.0f, [&](const Vec3& a, const Vec3& b, float& fr, Vec3& n) {
			bool actor = false;
			const bool hit = SceneRay(scene, a, b, fr, n, actor);
			if (hit) hitActor = actor;
			return hit;
		});

		ArcState::Frame f;
		f.active = true;
		f.charge = charge;
		f.speed = launch.velocity.Length();
		f.landed = arc.hit;
		f.hitsActor = arc.hit && hitActor;
		f.potsLeft = pots;
		for (const auto& p : arc.points) f.path.push_back(cam.Project(p));
		if (arc.hit) {
			for (const auto& p : Ring(arc.hitPoint, arc.hitNormal, blastRadius, 32)) f.ring.push_back(cam.Project(p));
		}
		return f;
	}

	// --- Software raster ----------------------------------------------------------------
	struct Image
	{
		std::vector<float> rgb = std::vector<float>(kW * kH * 3);
	};

	void Background(Image& img, const Camera& cam, const Scene& s)
	{
		for (int y = 0; y < kH; ++y)
			for (int x = 0; x < kW; ++x) {
				float* p = &img.rgb[(y * kW + x) * 3];
				// cast a ray per pixel against the ground/wall for a readable backdrop
				const float f = 0.5f / std::tan(cam.vfov * 0.5f);
				const float sx = (x + 0.5f) / kW - 0.5f, sy = 0.5f - (y + 0.5f) / kH;
				const float aspect = static_cast<float>(kW) / kH;
				Vec3 dir = cam.Forward() * f + cam.Right() * (sx * aspect) + cam.Up() * sy;
				dir = dir.Normalized();
				float tGround = dir.z < 0 ? -cam.pos.z / dir.z : 1e9f;
				float tWall = (s.wallY > 0 && dir.y > 0) ? (s.wallY - cam.pos.y) / dir.y : 1e9f;
				if (tWall < 1e9f && cam.pos.z + dir.z * tWall > s.wallTop) tWall = 1e9f;
				if (tWall < tGround) {
					const Vec3 h = cam.pos + dir * tWall;
					const bool brick = (static_cast<int>(h.x / 60.0f + 1000) + static_cast<int>(h.z / 30.0f)) % 2 == 0;
					p[0] = brick ? 92 : 84; p[1] = brick ? 86 : 79; p[2] = brick ? 80 : 74;
				} else if (tGround < 1e9f) {
					const Vec3 h = cam.pos + dir * tGround;
					const bool check = (static_cast<int>(std::floor(h.x / 100.0f)) + static_cast<int>(std::floor(h.y / 100.0f))) % 2 == 0;
					const float fog = std::clamp(tGround / 6000.0f, 0.0f, 1.0f);
					p[0] = (check ? 74 : 66) * (1 - fog) + 150 * fog;
					p[1] = (check ? 86 : 78) * (1 - fog) + 160 * fog;
					p[2] = (check ? 62 : 56) * (1 - fog) + 170 * fog;
				} else {
					const float t = std::clamp(sy * 2.0f, 0.0f, 1.0f);
					p[0] = 150 - 60 * t; p[1] = 165 - 50 * t; p[2] = 180 - 30 * t;
				}
				// actor: simple dark silhouette
				if (s.target.y > 0) {
					const auto base = cam.Project(s.target);
					const auto top = cam.Project(s.target + Vec3{ 0, 0, 130 });
					const auto side = cam.Project(s.target + Vec3{ 40, 0, 0 });
					const float px = (x + 0.5f) / kW, py = (y + 0.5f) / kH;
					if (base.visible && py <= base.y && py >= top.y && std::fabs(px - base.x) <= std::fabs(side.x - base.x)) {
						p[0] = 45; p[1] = 35; p[2] = 32;
					}
				}
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

	void Save(const Image& img, const std::string& path)
	{
		FILE* f = std::fopen(path.c_str(), "wb");
		std::fprintf(f, "P6\n%d %d\n255\n", kW, kH);
		for (float v : img.rgb) std::fputc(static_cast<unsigned char>(std::clamp(v, 0.0f, 255.0f)), f);
		std::fclose(f);
	}

	void RenderCase(const std::string& outDir, const char* name, const Camera& cam, const Scene& scene, float charge, int pots)
	{
		ImGui::GetIO().DisplaySize = { static_cast<float>(kW), static_cast<float>(kH) };
		ImGui::NewFrame();
		const auto frame = MakeFrame(cam, scene, charge, pots, 160.0f);
		ArcDraw::Style style;
		ArcDraw::Draw(ImGui::GetForegroundDrawList(), kW, kH, frame, style);
		ImGui::Render();
		ImDrawData* dd = ImGui::GetDrawData();
		HandleTextureRequests(dd);

		Image img;
		Background(img, cam, scene);
		Raster(img, dd);
		Save(img, outDir + "/" + name + ".ppm");
		std::printf("wrote %s (speed %.0f, %s%s)\n", name, frame.speed, frame.landed ? "lands" : "no landing in 4 s",
			frame.hitsActor ? ", hits actor" : "");
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
	cfg.SizePixels = 20.0f;
	io.Fonts->AddFontDefaultVector(&cfg);
	{
		io.DisplaySize = { static_cast<float>(kW), static_cast<float>(kH) };
		ImGui::NewFrame();
		ImGui::Render();
		HandleTextureRequests(ImGui::GetDrawData());
	}

	Camera cam;
	Scene  open;

	RenderCase(outDir, "1_short_lob", cam, open, 0.15f, 6);
	RenderCase(outDir, "2_half_windup", cam, open, 0.5f, 6);
	RenderCase(outDir, "3_full_windup", cam, open, 1.0f, 6);

	Scene wall;
	wall.wallY = 700.0f;
	wall.wallTop = 300.0f;
	RenderCase(outDir, "4_hits_wall", cam, wall, 0.8f, 3);

	Scene bandit;
	bandit.target = { -20.0f, 1230.0f, 0.0f };
	RenderCase(outDir, "5_hits_actor", cam, bandit, 0.75f, 1);

	Camera up = cam;
	up.pitchDown = -0.35f;  // looking up: long lob that leaves the traced time
	RenderCase(outDir, "6_into_sky", up, open, 1.0f, 6);

	ImGui::DestroyContext();
	return 0;
}
