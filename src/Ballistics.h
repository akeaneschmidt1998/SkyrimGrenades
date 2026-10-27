#pragma once
// Throw arc math: wind-up charge to speed, aim loft, Skyrim projectile angles, arc tracing and
// fitting the engine's real gravity from observed projectile positions.
// Plain std only (no game types) so tests/ballistics_test.cpp checks it without Skyrim.
//
// Skyrim world space: Z up, Y north, 1 unit = 1.428 cm (havok world scale 0.0142875 m/unit).

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace Ballistics
{
	struct Vec3
	{
		float x = 0.0f, y = 0.0f, z = 0.0f;

		Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
		Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
		Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }
		float Dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
		float Length() const { return std::sqrt(Dot(*this)); }
		Vec3  Normalized() const
		{
			const float l = Length();
			return l > 1e-6f ? *this * (1.0f / l) : Vec3{ 0.0f, 1.0f, 0.0f };
		}
	};

	inline constexpr float kPi = 3.14159265358979f;

	// 9.81 m/s^2 in game units per second squared. The engine's value is measured on the first
	// throw (FitGravity) and replaces this, so the preview matches what the engine really does.
	inline constexpr float kDefaultGravityUnits = 9.81f / 0.0142875f;  // ~686.6

	// Wind-up charge (0..1) to launch speed. Ease-out: the first half of the wind-up gives most of
	// the distance, the rest fine-tunes long throws.
	inline float ChargeToSpeed(float a_charge, float a_minSpeed, float a_maxSpeed)
	{
		const float c = std::clamp(a_charge, 0.0f, 1.0f);
		const float eased = 1.0f - (1.0f - c) * (1.0f - c);
		return a_minSpeed + (a_maxSpeed - a_minSpeed) * eased;
	}

	// Skyrim projectile rotation: x = pitch (positive looks DOWN), z = heading (clockwise from +Y).
	struct Angles
	{
		float x = 0.0f;
		float z = 0.0f;
	};

	inline Angles ToAngles(const Vec3& a_dir)
	{
		const Vec3 n = a_dir.Normalized();
		return { -std::asin(std::clamp(n.z, -1.0f, 1.0f)), std::atan2(n.x, n.y) };
	}

	inline Vec3 FromAngles(const Angles& a_angles)
	{
		return { std::sin(a_angles.z) * std::cos(a_angles.x),
			std::cos(a_angles.z) * std::cos(a_angles.x),
			-std::sin(a_angles.x) };
	}

	// Tilts an aim direction upward by a_loftRad (keeps the heading). Clamped short of straight up.
	inline Vec3 Loft(const Vec3& a_forward, float a_loftRad)
	{
		const Vec3  n = a_forward.Normalized();
		const float pitchUp = std::asin(std::clamp(n.z, -1.0f, 1.0f)) + a_loftRad;
		const float p = std::clamp(pitchUp, -1.45f, 1.45f);
		const float yaw = std::atan2(n.x, n.y);
		return { std::sin(yaw) * std::cos(p), std::cos(yaw) * std::cos(p), std::sin(p) };
	}

	struct Launch
	{
		Vec3  origin;
		Vec3  velocity;
		float gravity = kDefaultGravityUnits;  // units/s^2, pulls toward -Z
	};

	inline Vec3 PositionAt(const Launch& a_launch, float a_t)
	{
		return { a_launch.origin.x + a_launch.velocity.x * a_t,
			a_launch.origin.y + a_launch.velocity.y * a_t,
			a_launch.origin.z + a_launch.velocity.z * a_t - 0.5f * a_launch.gravity * a_t * a_t };
	}

	struct ArcResult
	{
		std::vector<Vec3> points;  // origin first; ends at the hit point when there is one
		bool              hit = false;
		Vec3              hitPoint;
		Vec3              hitNormal{ 0.0f, 0.0f, 1.0f };
		float             hitTime = 0.0f;
	};

	// Walks the arc in a_step-second segments, asking a_ray(from, to, outFraction, outNormal)
	// whether each segment hits something (a world raycast in game, a plane in tests).
	template <class RayFn>
	ArcResult Trace(const Launch& a_launch, float a_step, float a_maxTime, RayFn&& a_ray)
	{
		ArcResult out;
		out.points.push_back(a_launch.origin);
		Vec3 prev = a_launch.origin;
		for (float t = a_step; t <= a_maxTime + 1e-4f; t += a_step) {
			const Vec3 next = PositionAt(a_launch, t);
			float      fraction = 1.0f;
			Vec3       normal{ 0.0f, 0.0f, 1.0f };
			if (a_ray(prev, next, fraction, normal)) {
				fraction = std::clamp(fraction, 0.0f, 1.0f);
				out.hit = true;
				out.hitPoint = prev + (next - prev) * fraction;
				out.hitNormal = normal;
				out.hitTime = t - a_step + a_step * fraction;
				out.points.push_back(out.hitPoint);
				return out;
			}
			out.points.push_back(next);
			prev = next;
		}
		return out;
	}

	// Points on a horizontal-ish ring around a_center (perpendicular to a_normal), for drawing
	// the blast radius where the pot will land.
	inline std::vector<Vec3> Ring(const Vec3& a_center, const Vec3& a_normal, float a_radius, int a_segments)
	{
		const Vec3 n = a_normal.Normalized();
		// any vector not parallel to n
		const Vec3 helper = std::fabs(n.z) < 0.9f ? Vec3{ 0.0f, 0.0f, 1.0f } : Vec3{ 1.0f, 0.0f, 0.0f };
		// u = normalize(helper x n), v = n x u
		Vec3 u{ helper.y * n.z - helper.z * n.y, helper.z * n.x - helper.x * n.z, helper.x * n.y - helper.y * n.x };
		u = u.Normalized();
		const Vec3 v{ n.y * u.z - n.z * u.y, n.z * u.x - n.x * u.z, n.x * u.y - n.y * u.x };
		std::vector<Vec3> pts;
		pts.reserve(static_cast<std::size_t>(a_segments) + 1);
		for (int i = 0; i <= a_segments; ++i) {
			const float a = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(a_segments);
			pts.push_back(a_center + u * (std::cos(a) * a_radius) + v * (std::sin(a) * a_radius));
		}
		return pts;
	}

	// --- Measuring the engine ------------------------------------------------------------
	struct Sample
	{
		float t = 0.0f;
		Vec3  p;
	};

	struct Fit
	{
		float gravity = 0.0f;          // units/s^2 (positive = pulls down)
		float horizontalSpeed = 0.0f;  // units/s
		float launchVz = 0.0f;         // vertical speed at t = 0
	};

	// Least-squares fit of z(t) = c + b t - g/2 t^2 and x(t), y(t) linear over the samples.
	// Needs at least 4 samples spread over > 0.1 s; returns nothing otherwise.
	inline std::optional<Fit> FitFlight(const std::vector<Sample>& a_samples)
	{
		if (a_samples.size() < 4) {
			return std::nullopt;
		}
		const float t0 = a_samples.front().t;
		if (a_samples.back().t - t0 < 0.1f) {
			return std::nullopt;
		}
		// sums for the normal equations (double for stability)
		double s0 = 0, s1 = 0, s2 = 0, s3 = 0, s4 = 0, z0 = 0, z1 = 0, z2 = 0;
		double x0 = 0, x1 = 0, y0 = 0, y1 = 0;
		for (const auto& s : a_samples) {
			const double t = s.t - t0;
			s0 += 1; s1 += t; s2 += t * t; s3 += t * t * t; s4 += t * t * t * t;
			z0 += s.p.z; z1 += s.p.z * t; z2 += s.p.z * t * t;
			x0 += s.p.x; x1 += s.p.x * t; y0 += s.p.y; y1 += s.p.y * t;
		}
		// solve [s0 s1 s2; s1 s2 s3; s2 s3 s4] [c b k] = [z0 z1 z2], with z = c + b t + k t^2
		const double det = s0 * (s2 * s4 - s3 * s3) - s1 * (s1 * s4 - s3 * s2) + s2 * (s1 * s3 - s2 * s2);
		if (std::fabs(det) < 1e-12) {
			return std::nullopt;
		}
		const double detB = s0 * (z1 * s4 - s3 * z2) - z0 * (s1 * s4 - s3 * s2) + s2 * (s1 * z2 - z1 * s2);
		const double detK = s0 * (s2 * z2 - z1 * s3) - s1 * (s1 * z2 - z1 * s2) + z0 * (s1 * s3 - s2 * s2);
		const double b = detB / det;
		const double k = detK / det;

		const double denom = s0 * s2 - s1 * s1;
		if (std::fabs(denom) < 1e-12) {
			return std::nullopt;
		}
		const double vx = (s0 * x1 - s1 * x0) / denom;
		const double vy = (s0 * y1 - s1 * y0) / denom;

		Fit f;
		f.gravity = static_cast<float>(-2.0 * k);
		f.horizontalSpeed = static_cast<float>(std::sqrt(vx * vx + vy * vy));
		f.launchVz = static_cast<float>(b);
		return f;
	}
}
