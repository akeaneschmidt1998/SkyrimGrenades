// Offline checks for Ballistics.h (no Skyrim needed). Exit code 0 = all passed.
#include "Ballistics.h"

#include <cstdio>
#include <random>

using namespace Ballistics;

namespace
{
	int g_failures = 0;

	void Check(bool a_ok, const char* a_what)
	{
		std::printf("%s  %s\n", a_ok ? "ok  " : "FAIL", a_what);
		if (!a_ok) {
			++g_failures;
		}
	}

	bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

	// Ground plane at z = 0 for Trace.
	bool GroundRay(const Vec3& a_from, const Vec3& a_to, float& a_fraction, Vec3& a_normal)
	{
		if (a_from.z > 0.0f && a_to.z <= 0.0f) {
			a_fraction = a_from.z / (a_from.z - a_to.z);
			a_normal = { 0.0f, 0.0f, 1.0f };
			return true;
		}
		return false;
	}
}

int main()
{
	// Charge: ends map to min/max, ease-out puts the midpoint past halfway.
	Check(Near(ChargeToSpeed(0.0f, 600, 1600), 600, 1e-3f), "charge 0 = min speed");
	Check(Near(ChargeToSpeed(1.0f, 600, 1600), 1600, 1e-3f), "charge 1 = max speed");
	Check(ChargeToSpeed(0.5f, 600, 1600) > 1100, "half charge gives more than half the extra speed");
	Check(Near(ChargeToSpeed(2.0f, 600, 1600), 1600, 1e-3f), "charge clamps above 1");

	// Angles: round trip, and the sign conventions Skyrim uses.
	{
		const Vec3 dirs[] = { { 0, 1, 0 }, { 1, 0, 0 }, { 0.3f, -0.8f, 0.5f }, { -0.6f, 0.2f, -0.4f } };
		bool       ok = true;
		for (const auto& d : dirs) {
			const Vec3 back = FromAngles(ToAngles(d));
			const Vec3 n = d.Normalized();
			ok = ok && Near(back.x, n.x, 1e-4f) && Near(back.y, n.y, 1e-4f) && Near(back.z, n.z, 1e-4f);
		}
		Check(ok, "ToAngles/FromAngles round trip");
		Check(ToAngles({ 0, 1, 1 }).x < 0.0f, "aiming up gives negative pitch (Skyrim: positive = down)");
		Check(Near(ToAngles({ 1, 0, 0 }).z, kPi / 2, 1e-4f), "heading east = +90 degrees");
	}

	// Loft raises the pitch and keeps the heading.
	{
		const Vec3 fwd{ 0.0f, 1.0f, 0.0f };
		const Vec3 l = Loft(fwd, 15.0f * kPi / 180.0f);
		Check(Near(std::asin(l.z), 15.0f * kPi / 180.0f, 1e-4f), "loft adds 15 degrees of pitch");
		Check(Near(std::atan2(l.x, l.y), 0.0f, 1e-4f), "loft keeps heading");
		Check(Loft({ 0, 0.1f, 1 }, 1.0f).z < 1.0f, "loft never goes straight up");
	}

	// Trace on flat ground matches the closed-form landing point.
	{
		Launch launch;
		launch.origin = { 0.0f, 0.0f, 100.0f };
		launch.velocity = Loft({ 0, 1, 0 }, kPi / 4) * 1000.0f;
		launch.gravity = kDefaultGravityUnits;
		const auto  r = Trace(launch, 1.0f / 30.0f, 5.0f, GroundRay);
		const float vz = launch.velocity.z, g = launch.gravity;
		const float tLand = (vz + std::sqrt(vz * vz + 2 * g * 100.0f)) / g;
		Check(r.hit, "arc hits the ground");
		Check(Near(r.hitTime, tLand, 0.01f), "hit time matches closed form");
		Check(Near(r.hitPoint.y, launch.velocity.y * tLand, 5.0f), "landing distance matches closed form");
		Check(Near(r.hitPoint.z, 0.0f, 1e-3f), "landing point is on the ground");
		Check(r.points.size() > 10 && Near(r.points.back().y, r.hitPoint.y, 1e-3f), "polyline ends at the hit");
	}

	// A throw into the sky with nothing to hit stops at maxTime.
	{
		Launch launch;
		launch.origin = { 0, 0, 0 };
		launch.velocity = { 0, 0, 100000 };
		const auto r = Trace(launch, 0.1f, 1.0f, GroundRay);
		Check(!r.hit && r.points.size() == 11, "no hit: samples up to maxTime");
	}

	// Ring lies on the plane at the right radius.
	{
		const auto ring = Ring({ 10, 20, 30 }, { 0, 0, 1 }, 150.0f, 16);
		bool       ok = ring.size() == 17;
		for (const auto& p : ring) {
			ok = ok && Near(p.z, 30.0f, 1e-3f) && Near((p - Vec3{ 10, 20, 30 }).Length(), 150.0f, 1e-2f);
		}
		Check(ok, "ring is flat with the requested radius");
		const auto tilted = Ring({ 0, 0, 0 }, { 1, 0, 0 }, 50.0f, 8);
		bool       ok2 = true;
		for (const auto& p : tilted) {
			ok2 = ok2 && Near(p.x, 0.0f, 1e-3f);
		}
		Check(ok2, "ring follows a wall's normal");
	}

	// Fitting recovers gravity and speed from noisy samples (what the plugin logs after a throw).
	{
		std::mt19937                    rng(7);
		std::normal_distribution<float> noise(0.0f, 0.5f);
		Launch                          launch;
		launch.origin = { 100, 200, 300 };
		launch.velocity = { 600, 800, 400 };  // horizontal 1000
		launch.gravity = 520.0f;              // pretend the engine differs from our default
		std::vector<Sample> samples;
		for (int i = 0; i < 25; ++i) {
			const float t = 5.0f + i / 60.0f;  // absolute timestamps; the fit re-bases them
			Vec3        p = PositionAt(launch, t - 5.0f);
			p.z += noise(rng);
			samples.push_back({ t, p });
		}
		const auto fit = FitFlight(samples);
		Check(fit.has_value(), "fit succeeds");
		Check(fit && Near(fit->gravity, 520.0f, 15.0f), "fit recovers gravity");
		Check(fit && Near(fit->horizontalSpeed, 1000.0f, 2.0f), "fit recovers horizontal speed");
		Check(fit && Near(fit->launchVz, 400.0f, 10.0f), "fit recovers vertical launch speed");
		Check(!FitFlight({ samples.begin(), samples.begin() + 3 }).has_value(), "fit refuses too few samples");
	}

	std::printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "all passed", g_failures, g_failures == 1 ? "" : "s");
	return g_failures ? 1 : 0;
}
