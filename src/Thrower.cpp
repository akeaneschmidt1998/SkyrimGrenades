#include "Thrower.h"

#include "ArcState.h"
#include "Ballistics.h"
#include "Config.h"
#include "Crafting.h"
#include "Forms.h"

#include <xmmintrin.h>

namespace
{
	using Ballistics::Vec3;

	constexpr std::uint32_t kMouseOffset = 256;

	Vec3         ToVec(const RE::NiPoint3& p) { return { p.x, p.y, p.z }; }
	RE::NiPoint3 ToNi(const Vec3& v) { return { v.x, v.y, v.z }; }

	void Report(const std::string& a_msg, bool a_hud)
	{
		logger::info("{}", a_msg);
		if (Config::printToConsole) {
			if (auto* console = RE::ConsoleLog::GetSingleton()) {
				console->Print("[Fire Pots] %s", a_msg.c_str());
			}
		}
		if (a_hud && Config::showNotification) {
			RE::SendHUDMessage::ShowHUDMessage(a_msg.c_str());
		}
	}

	bool MatchesKey(const RE::ButtonEvent* a_button, std::uint32_t a_key)
	{
		if (a_key == 0) {
			return false;  // unbound
		}
		switch (a_button->GetDevice()) {
		case RE::INPUT_DEVICE::kKeyboard:
			return a_key < kMouseOffset && a_button->GetIDCode() == a_key;
		case RE::INPUT_DEVICE::kMouse:
			return a_key >= kMouseOffset && a_button->GetIDCode() == a_key - kMouseOffset;
		default:
			return false;
		}
	}

	// --- Input: the sink only records what happened; Tick acts on it (both on the main thread) ---
	struct InputFlags
	{
		std::atomic<bool> keyDown{ false };   // throw key currently held
		std::atomic<bool> pressed{ false };   // throw key went down since the last tick
		std::atomic<bool> cancel{ false };    // block pressed since the last tick
		std::atomic<bool> reload{ false };
		std::atomic<bool> cycle{ false };     // cycle key pressed since the last tick
		std::atomic<bool> craft{ false };     // craft key pressed since the last tick
	};
	InputFlags g_input;

	class InputSink final : public RE::BSTEventSink<RE::InputEvent*>
	{
	public:
		static InputSink* GetSingleton()
		{
			static InputSink instance;
			return &instance;
		}

		RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override
		{
			if (!a_event) {
				return RE::BSEventNotifyControl::kContinue;
			}
			for (auto* e = *a_event; e; e = e->next) {
				auto* button = e->GetEventType() == RE::INPUT_EVENT_TYPE::kButton ? e->AsButtonEvent() : nullptr;
				if (!button) {
					continue;
				}
				if (MatchesKey(button, Config::throwKey)) {
					if (button->IsDown()) {
						g_input.keyDown = true;
						g_input.pressed = true;
					} else if (button->IsUp()) {
						g_input.keyDown = false;
					}
					continue;
				}
				if (button->IsDown() && MatchesKey(button, Config::craftKey)) {
					g_input.craft = true;
					continue;
				}
				if (button->IsDown() && MatchesKey(button, Config::cycleKey)) {
					g_input.cycle = true;
					continue;
				}
				if (button->IsDown() && MatchesKey(button, Config::reloadConfigKey)) {
					g_input.reload = true;
					continue;
				}
				if (Config::cancelOnBlock && button->IsDown()) {
					auto* events = RE::UserEvents::GetSingleton();
					if (events && button->QUserEvent() == events->leftAttack) {  // block with a one-hander
						g_input.cancel = true;
					}
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}

	private:
		InputSink() = default;
	};

	// --- Pot types ---------------------------------------------------------------------------
	std::size_t g_typeIndex = 0;  // selected pot type (Forms::Types())

	Forms::PotType& CurrentType() { return Forms::Types()[g_typeIndex % Forms::Types().size()]; }

	int CountItem(RE::PlayerCharacter* a_player, RE::TESBoundObject* a_item)
	{
		if (!a_item) {
			return 0;
		}
		const auto counts = a_player->GetInventoryCounts([a_item](RE::TESBoundObject& a_obj) { return &a_obj == a_item; });
		const auto it = counts.find(a_item);
		return it == counts.end() ? 0 : it->second;
	}

	int CountPots(RE::PlayerCharacter* a_player, const Forms::PotType& a_type)
	{
		return a_type.Ready() ? CountItem(a_player, a_type.item) : 0;
	}

	// Moves the selection to the next type the player carries (starting after the current one when
	// a_advance, else at it). Returns false if they carry none at all.
	bool SelectCarriedType(RE::PlayerCharacter* a_player, bool a_advance)
	{
		const auto n = Forms::Types().size();
		for (std::size_t step = a_advance ? 1 : 0; step < n + (a_advance ? 1 : 0); ++step) {
			const auto i = (g_typeIndex + step) % n;
			if (CountPots(a_player, Forms::Types()[i]) > 0) {
				g_typeIndex = i;
				return true;
			}
		}
		return false;
	}

	// --- Engine calibration ---------------------------------------------------------------
	// Learned from the first throw(s): units/s^2 of gravity per 1.0 of the projectile's Gravity
	// field, how the engine scales the speed we set, and which way its pitch angle points.
	float g_gravityPerUnit = 0.0f;  // 0 = not measured yet
	float g_speedScale = 1.0f;      // engine launch speed / base speed we set
	float g_pitchSign = 1.0f;       // 1 = Skyrim convention (positive pitch = down), -1 = inverted

	float PreviewGravity()
	{
		const float perUnit = Config::gravityUnits > 0.0f ? Config::gravityUnits :
		                      g_gravityPerUnit > 0.0f     ? g_gravityPerUnit :
		                                                    Ballistics::kDefaultGravityUnits;
		auto* proj = CurrentType().projectile;
		return perUnit * (proj ? proj->data.gravity : 1.0f);
	}

	struct Flight
	{
		bool                           active = false;
		RE::ProjectileHandle           handle;
		float                          time = 0.0f;
		float                          baseSpeedSet = 0.0f;  // what we wrote into the projectile form
		float                          baseGravity = 1.0f;
		Vec3                           dir;                  // unit launch direction we intended
		std::vector<Ballistics::Sample> samples;
	};
	Flight g_flight;

	void FinishFlight()
	{
		g_flight.active = false;
		const auto fit = Ballistics::FitFlight(g_flight.samples);
		if (!fit) {
			logger::info("Flight check: not enough flight to measure ({} samples); will retry on the next throw",
				g_flight.samples.size());
			return;
		}
		const float horiz = std::sqrt(g_flight.dir.x * g_flight.dir.x + g_flight.dir.y * g_flight.dir.y);
		const float askedH = g_flight.baseSpeedSet * horiz;
		const float askedVz = g_flight.baseSpeedSet * g_flight.dir.z;
		const float perUnit = fit->gravity / (std::max)(g_flight.baseGravity, 0.01f);

		logger::info("Flight check: gravity {:.0f} u/s^2 ({:.0f} per unit, preview used {:.0f}), "
					 "horizontal {:.0f} u/s (set {:.0f}), vertical {:.0f} u/s (set {:.0f}), {} samples",
			fit->gravity, perUnit, PreviewGravity() / (std::max)(g_flight.baseGravity, 0.01f),
			fit->horizontalSpeed, askedH, fit->launchVz, askedVz, g_flight.samples.size());

		if (!Config::autoCalibrate) {
			return;
		}
		// Pitch sign: a clearly upward throw that went clearly downward (or the reverse).
		if (std::fabs(askedVz) > 150.0f && std::fabs(fit->launchVz) > 150.0f && (askedVz > 0) != (fit->launchVz > 0)) {
			g_pitchSign = -g_pitchSign;
			Report("Calibration: launch pitch was inverted, corrected (please report this in the log)", false);
		}
		if (Config::gravityUnits <= 0.0f && perUnit > 50.0f && perUnit < 5000.0f) {
			g_gravityPerUnit = perUnit;
		}
		if (askedH > 100.0f) {
			const float scale = fit->horizontalSpeed / askedH * g_speedScale;
			if (scale > 0.25f && scale < 4.0f) {
				g_speedScale = scale;
			}
		}
		Report(fmt::format("Calibrated the arc: gravity {:.0f} u/s^2, speed x{:.2f}. Put fGravityUnits = {:.0f} in "
						   "FirePots.ini to skip this.",
				   g_gravityPerUnit > 0.0f ? g_gravityPerUnit : PreviewGravity(), g_speedScale,
				   g_gravityPerUnit > 0.0f ? g_gravityPerUnit : PreviewGravity()),
			false);
	}

	void TrackFlight(float a_dt)
	{
		if (!g_flight.active) {
			return;
		}
		g_flight.time += a_dt;
		auto proj = g_flight.handle.get();
		if (!proj || g_flight.time > 0.7f) {
			FinishFlight();
			return;
		}
		const Vec3 p = ToVec(proj->GetPosition());
		if (!g_flight.samples.empty()) {
			const Vec3 d = p - g_flight.samples.back().p;
			if (d.Length() < 0.01f) {
				// not moving: not launched yet (first frame) or already exploded
				if (g_flight.samples.size() >= 4) {
					FinishFlight();
				}
				return;
			}
		}
		g_flight.samples.push_back({ g_flight.time, p });
	}

	// --- Aim -------------------------------------------------------------------------------
	struct Aim
	{
		Vec3 origin;
		Vec3 forward;  // camera forward (where the crosshair points)
	};

	std::optional<Aim> GetAim(RE::PlayerCharacter* a_player)
	{
		auto* camera = RE::PlayerCamera::GetSingleton();
		if (!camera || !camera->cameraRoot) {
			return std::nullopt;
		}
		const auto& w = camera->cameraRoot->world;
		// node axes are the matrix columns: X right, Y forward, Z up
		Aim aim;
		aim.forward = Vec3{ w.rotate.entry[0][1], w.rotate.entry[1][1], w.rotate.entry[2][1] }.Normalized();
		const Vec3 camPos = ToVec(w.translate);

		const bool firstPerson = camera->IsInFirstPerson();
		RE::NiAVObject* hand = nullptr;
		if (auto* root = a_player->Get3D(firstPerson)) {
			hand = root->GetObjectByName(RE::BSFixedString(Config::throwNode.c_str()));
		}
		if (hand) {
			aim.origin = ToVec(hand->world.translate);
		} else {
			// no such node (custom skeleton, typo in sThrowNode): just below and ahead of the camera
			aim.origin = camPos + aim.forward * 40.0f + Vec3{ 0.0f, 0.0f, -20.0f };
		}
		return aim;
	}

	// --- Arc preview -----------------------------------------------------------------------
	struct RayCtx
	{
		RE::bhkWorld* world = nullptr;
		std::uint32_t filter = 0;
		bool          hitActor = false;
	};

	bool CastRay(RayCtx& a_ctx, const Vec3& a_from, const Vec3& a_to, float& a_fraction, Vec3& a_normal)
	{
		const float     s = RE::bhkWorld::GetWorldScale();
		RE::bhkPickData pick{};
		pick.rayInput.from = RE::hkVector4(a_from.x * s, a_from.y * s, a_from.z * s, 0.0f);
		pick.rayInput.to = RE::hkVector4(a_to.x * s, a_to.y * s, a_to.z * s, 0.0f);
		pick.rayInput.filterInfo.filter = a_ctx.filter;
		{
			RE::BSReadLockGuard lock(a_ctx.world->worldLock);
			a_ctx.world->PickObject(pick);
		}
		if (!pick.rayOutput.HasHit()) {
			return false;
		}
		a_fraction = pick.rayOutput.hitFraction;
		alignas(16) float n[4];
		_mm_store_ps(n, pick.rayOutput.normal.quad);
		a_normal = { n[0], n[1], n[2] };
		auto* ref = RE::TESHavokUtilities::FindCollidableRef(*pick.rayOutput.rootCollidable);
		a_ctx.hitActor = ref && ref->As<RE::Actor>() != nullptr;
		return true;
	}

	ArcState::ScreenPt Project(RE::NiCamera* a_camera, const Vec3& a_point)
	{
		float x = 0.0f, y = 0.0f, z = 0.0f;
		if (!a_camera->WorldPtToScreenPt3(ToNi(a_point), x, y, z, 1e-5f) || z < 0.0f) {
			return {};
		}
		return { x, 1.0f - y, true };  // the engine's y grows upward
	}

	struct Plan
	{
		Ballistics::Launch launch;
		float              speed = 0.0f;
		Vec3               dir;
	};

	Plan MakePlan(const Aim& a_aim, float a_charge)
	{
		Plan p;
		p.speed = Ballistics::ChargeToSpeed(a_charge, Config::minSpeed, Config::maxSpeed);
		p.dir = Ballistics::Loft(a_aim.forward, Config::loftDegrees * Ballistics::kPi / 180.0f);
		p.launch.origin = a_aim.origin;
		p.launch.velocity = p.dir * p.speed;
		p.launch.gravity = PreviewGravity();
		return p;
	}

	void PublishPreview(RE::PlayerCharacter* a_player, const Plan& a_plan, float a_charge, int a_pots)
	{
		ArcState::Frame frame;
		frame.potName = CurrentType().name;
		frame.frost = g_typeIndex == 1;
		frame.active = true;
		frame.charge = a_charge;
		frame.speed = a_plan.speed;
		frame.potsLeft = a_pots;

		auto*  cell = a_player->GetParentCell();
		RayCtx ctx;
		ctx.world = cell ? cell->GetbhkWorld() : nullptr;
		if (ctx.world) {
			RE::CFilter own{};
			a_player->GetCollisionFilterInfo(own);
			// projectile layer, in the player's own collision group so the ray ignores the player
			ctx.filter = (own.filter & 0xFFFF0000u) | static_cast<std::uint32_t>(RE::COL_LAYER::kProjectile);
		}
		bool hitActor = false;
		const auto arc = Ballistics::Trace(a_plan.launch, Config::arcStep, Config::arcMaxTime,
			[&](const Vec3& a_from, const Vec3& a_to, float& a_fraction, Vec3& a_normal) {
				if (!ctx.world) {
					return false;
				}
				const bool hit = CastRay(ctx, a_from, a_to, a_fraction, a_normal);
				hitActor = hit && ctx.hitActor;
				return hit;
			});
		frame.landed = arc.hit;
		frame.hitsActor = arc.hit && hitActor;

		if (auto* camera = RE::Main::WorldRootCamera()) {
			frame.path.reserve(arc.points.size());
			for (const auto& p : arc.points) {
				frame.path.push_back(Project(camera, p));
			}
			if (arc.hit && Config::blastRadius > 0.0f) {
				for (const auto& p : Ballistics::Ring(arc.hitPoint, arc.hitNormal, Config::blastRadius, 32)) {
					frame.ring.push_back(Project(camera, p));
				}
			}
		}
		ArcState::Publish(std::move(frame));
	}

	// --- The throw -------------------------------------------------------------------------
	void Launch(RE::PlayerCharacter* a_player, const Plan& a_plan, float a_charge)
	{
		auto& type = CurrentType();
		auto* base = type.projectile;
		// The engine reads the speed from the form when the projectile spawns; compensate for any
		// speed multiplier it applies on top (measured on the first throw).
		base->data.speed = a_plan.speed / g_speedScale;

		auto angles = Ballistics::ToAngles(a_plan.dir);
		angles.x *= g_pitchSign;
		RE::Projectile::ProjectileRot rot{ angles.x, angles.z };

		RE::Projectile::LaunchData data(base, a_player, ToNi(a_plan.launch.origin), rot);
		data.autoAim = false;  // go exactly where the arc said
		data.useOrigin = true;
		data.scale = Config::projectileScale;

		RE::ProjectileHandle handle;
		RE::Projectile::Launch(&handle, data);
		if (!handle) {
			Report("Fire Pot launch failed (see FirePots.log)", true);
			logger::error("Projectile::Launch returned no projectile");
			return;
		}
		a_player->RemoveItem(type.item, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);

		if (Config::autoCalibrate || g_gravityPerUnit <= 0.0f) {
			g_flight = Flight{};
			g_flight.active = true;
			g_flight.handle = handle;
			g_flight.baseSpeedSet = base->data.speed;
			g_flight.baseGravity = base->data.gravity;
			g_flight.dir = a_plan.dir;
		}
		logger::info("Threw a {}: {:.0f}% wind-up, {:.0f} u/s, pitch {:.1f} deg, from ({:.0f}, {:.0f}, {:.0f})",
			type.name, a_charge * 100.0f, a_plan.speed, std::asin(a_plan.dir.z) * 180.0f / Ballistics::kPi,
			a_plan.launch.origin.x, a_plan.launch.origin.y, a_plan.launch.origin.z);
	}

	// nullptr = may throw; "" = blocked silently; otherwise why not (shown to the player).
	const char* Blocker(RE::PlayerCharacter* a_player)
	{
		auto* ui = RE::UI::GetSingleton();
		auto* controls = RE::ControlMap::GetSingleton();
		if (!ui || !controls || ui->GameIsPaused() || ui->IsMenuOpen(RE::Console::MENU_NAME)) {
			return "";
		}
		if (!controls->IsMovementControlsEnabled() || !controls->IsLookingControlsEnabled()) {
			return "";  // dialogue, cutscenes, crafting...
		}
		if (a_player->IsOnMount()) {
			return "You can't throw while riding.";
		}
		const auto* state = a_player->AsActorState();
		if (state && state->IsSwimming()) {
			return "You can't throw while swimming.";
		}
		if (state && state->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal) {
			return "";
		}
		return nullptr;
	}

	enum class Phase
	{
		kIdle,
		kWinding,
	};

	struct ThrowState
	{
		Phase phase = Phase::kIdle;
		float held = 0.0f;      // seconds the key has been held
		float cooldown = 0.0f;  // seconds until the next throw is allowed
		int   pots = 0;         // counted when the wind-up starts
	};
	ThrowState g_throw;

	void StopWinding()
	{
		g_throw.phase = Phase::kIdle;
		g_throw.held = 0.0f;
		ArcState::Clear();
	}

	void Tick(RE::PlayerCharacter* a_player, float a_dt)
	{
		if (g_input.reload.exchange(false)) {
			Config::Load();
			Report("Settings reloaded", true);
		}

		TrackFlight(a_dt);
		g_throw.cooldown = (std::max)(0.0f, g_throw.cooldown - a_dt);

		const bool pressed = g_input.pressed.exchange(false);
		const bool cancel = g_input.cancel.exchange(false);

		if (g_input.craft.exchange(false) && g_throw.phase == Phase::kIdle) {
			auto* ui = RE::UI::GetSingleton();
			auto* controls = RE::ControlMap::GetSingleton();
			const bool free = ui && controls && !ui->GameIsPaused() && !ui->IsMenuOpen(RE::Console::MENU_NAME) &&
			                  controls->IsMovementControlsEnabled();  // not in dialogue, cutscenes, other menus
			if (free) {
				Crafting::Open(a_player);
			}
		}

		if (g_input.cycle.exchange(false) && Forms::Ready()) {
			if (SelectCarriedType(a_player, true)) {
				g_throw.pots = CountPots(a_player, CurrentType());
				Report(fmt::format("{} ({})", CurrentType().name, g_throw.pots), true);
			} else {
				Report("You have no Fire Pots or Frost Pots.", true);
			}
		}

		if (g_throw.phase == Phase::kIdle) {
			if (!pressed) {
				return;
			}
			if (!Forms::Ready()) {
				Report("FirePots.esp isn't loaded, so there's nothing to throw.", true);
				return;
			}
			if (const char* why = Blocker(a_player)) {
				if (*why) {
					Report(why, true);
				}
				return;
			}
			if (g_throw.cooldown > 0.0f) {
				return;
			}
			// throw the selected type; if you're out of it, switch to one you have
			const auto before = g_typeIndex;
			if (!SelectCarriedType(a_player, false)) {
				Report("You have no Fire Pots or Frost Pots.", true);
				return;
			}
			if (g_typeIndex != before) {
				Report(fmt::format("Switched to {}", CurrentType().name), false);
			}
			g_throw.pots = CountPots(a_player, CurrentType());
			g_throw.phase = Phase::kWinding;
			g_throw.held = 0.0f;
		}

		// --- winding up ---
		if (cancel || Blocker(a_player)) {
			if (cancel) {
				Report("Throw cancelled", false);
			}
			StopWinding();
			return;
		}
		g_throw.held += a_dt;
		const float charge = std::clamp(g_throw.held / Config::chargeSeconds, 0.0f, 1.0f);

		const auto aim = GetAim(a_player);
		if (!aim) {
			StopWinding();
			return;
		}
		const Plan plan = MakePlan(*aim, charge);

		if (!g_input.keyDown) {
			// released: throw, unless it was only a tap
			if (g_throw.held * 1000.0f < Config::minHoldMs) {
				Report("Hold the throw key to wind up, release to throw.", true);
			} else {
				Launch(a_player, plan, charge);
				g_throw.cooldown = Config::cooldownMs / 1000.0f;
			}
			StopWinding();
			return;
		}

		if (Config::showArc) {
			PublishPreview(a_player, plan, charge, g_throw.pots);
		}
	}

	// --- Hook ------------------------------------------------------------------------------
	struct PlayerUpdateHook
	{
		static void Update(RE::PlayerCharacter* a_this, float a_delta)
		{
			_Update(a_this, a_delta);
			Tick(a_this, a_delta);
		}

		static inline REL::Relocation<decltype(Update)> _Update;

		static void Install()
		{
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_PlayerCharacter[0] };
			_Update = vtbl.write_vfunc(0xAD, Update);
			logger::info("Hooked PlayerCharacter::Update");
		}
	};
}

namespace Thrower
{
	void InstallHooks()
	{
		if (REL::Module::IsVR()) {
			logger::critical("Skyrim VR is not supported (the update hook is SE/AE only)");
			return;
		}
		PlayerUpdateHook::Install();
	}

	void RegisterInput()
	{
		auto* devices = RE::BSInputDeviceManager::GetSingleton();
		if (!devices) {
			logger::error("BSInputDeviceManager not available; throwing disabled");
			return;
		}
		devices->AddEventSink(InputSink::GetSingleton());
		logger::info("Input listener registered (throw key {}, cycle key {})", Config::throwKey, Config::cycleKey);
	}

	void Reset()
	{
		StopWinding();
		g_flight = Flight{};
		g_input.keyDown = false;
		g_input.pressed = false;
		g_input.cancel = false;
		g_input.cycle = false;
		g_input.craft = false;
	}
}
