#include "Thrower.h"

#include "ArcState.h"
#include "Ballistics.h"
#include "Config.h"
#include "Crafting.h"
#include "CraftShared.h"
#include "Forms.h"

#include <map>
#include <xmmintrin.h>

#undef GetObject  // <Windows.h> renames it to GetObjectA, which breaks BGSDefaultObjectManager::GetObject

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
		std::atomic<bool> reload{ false };
		std::atomic<bool> cycle{ false };     // cycle key pressed since the last tick
		std::atomic<bool> craft{ false };     // craft key pressed since the last tick
		// attack buttons per hand (0 = right "Right Attack/Block", 1 = left "Left Attack/Block")
		std::atomic<bool> handDown[2]{ false, false };
		std::atomic<bool> handPressed[2]{ false, false };
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
			if (!a_event || CraftShared::open) {
				return RE::BSEventNotifyControl::kContinue;  // the bomb-making menu has the input
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
				if (auto* events = RE::UserEvents::GetSingleton()) {
					const auto& ev = button->QUserEvent();
					const int   hand = ev == events->rightAttack ? 0 : ev == events->leftAttack ? 1 : -1;
					if (hand >= 0) {
						if (button->IsDown()) {
							g_input.handDown[hand] = true;
							g_input.handPressed[hand] = true;
						} else if (button->IsUp()) {
							g_input.handDown[hand] = false;
						}
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

	// Which pot type a form is (-1 = not a pot).
	int PotTypeOf(const RE::TESForm* a_form)
	{
		const auto& types = Forms::Types();
		for (std::size_t i = 0; i < types.size(); ++i) {
			if (a_form && types[i].item == a_form) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	// Pot type equipped in that hand (-1 = none).
	int HandPot(RE::PlayerCharacter* a_player, bool a_left) { return PotTypeOf(a_player->GetEquippedObject(a_left)); }

	// Puts a pot of that type in the hand (like picking it in Favorites).
	void EquipPot(RE::PlayerCharacter* a_player, int a_type, bool a_left)
	{
		auto* mgr = RE::ActorEquipManager::GetSingleton();
		auto* dom = RE::BGSDefaultObjectManager::GetSingleton();
		auto* slot = dom ? dom->GetObject<RE::BGSEquipSlot>(a_left ? RE::DEFAULT_OBJECT::kLeftHandEquip : RE::DEFAULT_OBJECT::kRightHandEquip) : nullptr;
		if (mgr && slot) {
			mgr->EquipObject(a_player, Forms::Types()[a_type].item, nullptr, 1, slot);
		}
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
		if (Config::calibrateSpeed && askedH > 100.0f) {
			const float scale = fit->horizontalSpeed / askedH * g_speedScale;
			if (scale > 0.5f && scale < 2.0f) {
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

	// --- Oil: slicks left by oil bombs, set alight when a fire bomb bursts in one ---------------
	struct Live
	{
		RE::ProjectileHandle handle;
		int                  payload = 0;
		int                  strength = 0;
		bool                 debug = false;
		Vec3                 last;
	};
	struct Slick
	{
		Vec3  pos;
		int   strength = 0;
		float radius = 0.0f;
		float ttl = 0.0f;  // seconds left (the slick hazard lasts 60 s)
	};
	std::vector<Live>  g_live;
	std::vector<Slick> g_slicks;

	void ChainFrom(const Vec3& a_at, float a_radius);  // volatile bombs nearby go off too (below)
	void BurstLook(RE::PlayerCharacter* a_player, const Vec3& a_at, int a_payload);  // below

	void Track(const RE::ProjectileHandle& a_handle, const Forms::PotType& a_type)
	{
		Live l;
		l.handle = a_handle;
		l.payload = a_type.payload;
		l.strength = a_type.strength;
		l.debug = a_type.debug;
		if (auto p = a_handle.get()) {
			l.last = ToVec(p->GetPosition());
		}
		g_live.push_back(l);
	}

	// Smoke's second layer: a low mist dropped where the smoke bomb burst.
	void SmokeMist(RE::PlayerCharacter* a_player, const Vec3& a_at, int a_strength)
	{
		auto* base = Forms::smokeMistProjectile[std::clamp(a_strength, 0, Bombs::kStrengths - 1)];
		if (!base) {
			return;
		}
		RE::Projectile::ProjectileRot rot{ 1.5707963f * g_pitchSign, 0.0f };
		RE::Projectile::LaunchData    data(base, a_player, ToNi(a_at + Vec3{ 0.0f, 0.0f, 30.0f }), rot);
		data.autoAim = false;
		data.useOrigin = true;
		RE::ProjectileHandle h;
		RE::Projectile::Launch(&h, data);
	}

	// Fire's spread: a ring of burning-ground patches around a fire burst, so it covers the blast.
	// Drops one of the visual / fire "droppers" straight down onto a spot.
	void DropAt(RE::PlayerCharacter* a_player, RE::BGSProjectile* a_base, const Vec3& a_at)
	{
		if (!a_base) {
			return;
		}
		RE::Projectile::ProjectileRot rot{ 1.5707963f * g_pitchSign, 0.0f };
		RE::Projectile::LaunchData    data(a_base, a_player, ToNi(a_at + Vec3{ 0.0f, 0.0f, 60.0f }), rot);
		data.autoAim = false;
		data.useOrigin = true;
		RE::ProjectileHandle h;
		RE::Projectile::Launch(&h, data);
	}

	void Ring(RE::PlayerCharacter* a_player, RE::BGSProjectile* a_base, const Vec3& a_at, int a_count, float a_radius, float a_turn = 0.0f)
	{
		for (int i = 0; i < a_count; ++i) {
			const float a = 2.0f * Ballistics::kPi * (static_cast<float>(i) + a_turn) / static_cast<float>(a_count);
			DropAt(a_player, a_base, a_at + Vec3{ std::cos(a) * a_radius, std::sin(a) * a_radius, 0.0f });
		}
	}

	// Fire's look: mild = the centre patch only; moderate = 3 patches round the edge; major = 4 and a
	// big blaze in the middle.
	void FireSpread(RE::PlayerCharacter* a_player, const Vec3& a_at, int a_strength)
	{
		const int s = std::clamp(a_strength, 0, Bombs::kStrengths - 1);
		Ring(a_player, Forms::fireSpreadProjectile[s], a_at, Bombs::FireSpreadCount(s), Bombs::BlastRadius(Bombs::kFire, s), 0.5f * static_cast<float>(s));
		if (s == Bombs::kMajor) {
			DropAt(a_player, Forms::fireBigProjectile, a_at);
		}
	}

	// Oil's look: pools round the slick's edge (the centre one comes with the slick itself).
	void OilPools(RE::PlayerCharacter* a_player, const Vec3& a_at, int a_strength)
	{
		const int s = std::clamp(a_strength, 0, Bombs::kStrengths - 1);
		Ring(a_player, Forms::oilPoolProjectile, a_at, Bombs::OilPoolCount(s), Bombs::OilSlickRadius(s) * 0.8f);
	}

	void Ignite(RE::PlayerCharacter* a_player, const Slick& a_slick)
	{
		// the burning-oil burst in the middle (damage + a big fire), fire round the edge, a blaze on top
		DropAt(a_player, Forms::oilFireProjectile, a_slick.pos);
		Ring(a_player, Forms::fireSpreadProjectile[Bombs::kMajor], a_slick.pos, 4 + a_slick.strength, a_slick.radius * 0.8f);
		DropAt(a_player, Forms::fireBigProjectile, a_slick.pos);
		logger::info("Oil slick at ({:.0f}, {:.0f}, {:.0f}) ignited", a_slick.pos.x, a_slick.pos.y, a_slick.pos.z);
	}

	// Fire from anything else - spells, enchanted arrows, fire explosions and hazards near a slick.
	bool IsFire(const RE::MagicItem* a_magic)
	{
		if (!a_magic) {
			return false;
		}
		for (const auto* e : a_magic->effects) {
			if (e && e->baseEffect && e->baseEffect->data.resistVariable == RE::ActorValue::kResistFire) {
				return true;
			}
		}
		return false;
	}

	bool IsOurs(const RE::TESForm* a_base)
	{
		for (const auto& t : Forms::Types()) {
			if (t.projectile == a_base) {
				return true;
			}
		}
		return a_base == Forms::oilFireProjectile;
	}

	float g_fireScan = 0.0f;

	void IgniteFromOtherFire(RE::PlayerCharacter* a_player, float a_dt)
	{
		g_fireScan -= a_dt;
		if (g_slicks.empty() || g_fireScan > 0.0f) {
			return;
		}
		g_fireScan = 0.2f;
		auto* tes = RE::TES::GetSingleton();
		if (!tes) {
			return;
		}
		std::vector<std::pair<Vec3, Vec3>> fire;  // segments that are on fire (a point: both ends equal)
		tes->ForEachReferenceInRange(a_player, 6000.0f, [&](RE::TESObjectREFR* a_ref) {
			if (!a_ref || a_ref->IsDisabled() || a_ref->IsDeleted()) {
				return RE::BSContainer::ForEachResult::kContinue;
			}
			const Vec3 at = ToVec(a_ref->GetPosition());
			if (auto* proj = a_ref->As<RE::Projectile>()) {
				auto* base = a_ref->GetBaseObject();
				if (IsOurs(base)) {
					return RE::BSContainer::ForEachResult::kContinue;  // our own bombs are handled when they burst
				}
				auto& rd = proj->GetProjectileRuntimeData();
				const bool fiery = IsFire(rd.spell) || (rd.explosion && IsFire(rd.explosion->formEnchanting));
				if (fiery) {
					// a flame jet reaches ahead of where it starts
					Vec3 end = at;
					if (base && base->GetFormType() == RE::FormType::ProjectileFlame) {
						const auto ang = a_ref->GetAngle();
						const Vec3 fwd{ std::sin(ang.z) * std::cos(ang.x), std::cos(ang.z) * std::cos(ang.x), -std::sin(ang.x) };
						end = at + fwd * 450.0f;
					}
					fire.emplace_back(at, end);
				}
			} else if (auto* ex = a_ref->As<RE::Explosion>()) {
				auto* base = a_ref->GetBaseObject();
				auto* e = base ? base->As<RE::BGSExplosion>() : nullptr;
				if (e && IsFire(e->formEnchanting) && !IsOurs(base)) {
					fire.emplace_back(at, at);
				}
			} else if (auto* base = a_ref->GetBaseObject(); base && base->GetFormType() == RE::FormType::Hazard) {
				auto* hz = base->As<RE::BGSHazard>();
				if (hz && IsFire(hz->data.spell)) {
					fire.emplace_back(at, at);
				}
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		if (fire.empty()) {
			return;
		}
		auto distToSeg = [](const Vec3& p, const Vec3& a, const Vec3& b) {
			const Vec3  ab = b - a;
			const float len2 = ab.x * ab.x + ab.y * ab.y + ab.z * ab.z;
			float       t = len2 > 0.0f ? ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y + (p.z - a.z) * ab.z) / len2 : 0.0f;
			t = std::clamp(t, 0.0f, 1.0f);
			return (p - (a + ab * t)).Length();
		};
		for (auto it = g_slicks.begin(); it != g_slicks.end();) {
			bool lit = false;
			for (const auto& [a, b] : fire) {
				if (distToSeg(it->pos, a, b) <= it->radius + 40.0f) {
					lit = true;
					break;
				}
			}
			if (lit) {
				Ignite(a_player, *it);
				it = g_slicks.erase(it);
			} else {
				++it;
			}
		}
	}

	void UpdateOil(RE::PlayerCharacter* a_player, float a_dt)
	{
		for (auto& s : g_slicks) {
			s.ttl -= a_dt;
		}
		std::erase_if(g_slicks, [](const Slick& s) { return s.ttl <= 0.0f; });

		for (auto it = g_live.begin(); it != g_live.end();) {
			auto proj = it->handle.get();
			if (proj && !proj->IsDeleted() && !proj->IsDisabled()) {
				it->last = ToVec(proj->GetPosition());
				++it;
				continue;
			}
			// gone: it burst at its last position
			ChainFrom(it->last, Bombs::BlastRadius(it->payload, it->strength));
			BurstLook(a_player, it->last, it->payload);
			if (it->payload == Bombs::kFire && !it->debug) {
				FireSpread(a_player, it->last, it->strength);
			}
			if (it->payload == Bombs::kSmoke && !it->debug) {
				SmokeMist(a_player, it->last, it->strength);
			}
			if (it->payload == Bombs::kOil) {
				g_slicks.push_back({ it->last, it->strength, Bombs::OilSlickRadius(it->strength), 60.0f });
				OilPools(a_player, it->last, it->strength);
			} else if (it->payload == Bombs::kFire) {
				const float blast = Bombs::BlastRadius(Bombs::kFire, it->strength);
				for (auto s = g_slicks.begin(); s != g_slicks.end();) {
					if ((s->pos - it->last).Length() <= s->radius + blast) {
						Ignite(a_player, *s);
						s = g_slicks.erase(s);
					} else {
						++s;
					}
				}
			}
			it = g_live.erase(it);
		}
	}

	// --- Size: bombs fly and sit at Config::projectileScale of the container's size ------------
	// LaunchData's scale isn't used by the engine, so the projectile's node is scaled every frame.
	// Hidden projectiles (the one that sets off a fuse / set-down bomb) are kept at a speck and culled.
	struct Scaled
	{
		RE::ProjectileHandle handle;
		bool                 hidden = false;
	};
	std::vector<Scaled> g_scaled;

	void Shrink(RE::TESObjectREFR* a_ref, bool a_hidden = false)
	{
		const float s = a_hidden ? 0.01f : Config::projectileScale;
		if (auto* node = a_ref ? a_ref->Get3D() : nullptr) {
			if (a_hidden) {
				node->SetAppCulled(true);
			}
			if (std::fabs(node->local.scale - s) > 0.0001f) {
				node->local.scale = s;
				RE::NiUpdateData ud;
				node->Update(ud);
			}
		}
	}

	void UpdateScaled()
	{
		const bool shrinkThrown = std::fabs(Config::projectileScale - 1.0f) > 0.001f;
		std::erase_if(g_scaled, [&](const Scaled& a_s) {
			auto p = a_s.handle.get();
			if (!p || p->IsDeleted() || p->IsDisabled()) {
				return true;
			}
			if (a_s.hidden || shrinkThrown) {
				Shrink(p.get(), a_s.hidden);
			}
			return false;
		});
	}

	// --- The bomb in your hand: its pot model, hung on the hand's magic node while drawn ---------
	// (Casting art never showed, so the mod attaches the mesh itself.) Collision is stripped from
	// the copy so it can't drop out of the hand; fHandScale shrinks it, fHandOffset* nudge it.
	std::map<std::string, RE::NiPointer<RE::NiNode>> g_heldModels;  // loaded once per mesh

	RE::NiPointer<RE::NiAVObject> MakeHeld(const char* a_mesh, const std::string& a_name)
	{
		auto& cached = g_heldModels[a_mesh];
		if (!cached) {
			RE::BSModelDB::DBTraits::ArgsType args{};
			if (RE::BSModelDB::Demand(a_mesh, cached, args) != RE::BSResource::ErrorCode::kNone || !cached) {
				logger::warn("Bomb in hand: couldn't load '{}'", a_mesh);
				return nullptr;
			}
		}
		auto* copy = cached->Clone();
		RE::NiPointer<RE::NiAVObject> obj{ copy ? netimmerse_cast<RE::NiAVObject*>(copy) : nullptr };
		if (!obj) {
			return nullptr;
		}
		RE::BSVisit::TraverseScenegraphObjects(obj.get(), [](RE::NiAVObject* a_o) {
			if (a_o) {
				a_o->collisionObject.reset();
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		obj->name = a_name;
		return obj;
	}

	// Scale, offset and turn from the settings (applied every frame, so MCM changes show at once).
	void PoseHeld(RE::NiAVObject* a_obj)
	{
		constexpr float kDeg = 3.14159265f / 180.0f;
		a_obj->local.scale = Config::handScale;
		a_obj->local.translate = { Config::handOffset[0], Config::handOffset[1], Config::handOffset[2] };
		a_obj->local.rotate.SetEulerAnglesXYZ(Config::handRotation[0] * kDeg, Config::handRotation[1] * kDeg, Config::handRotation[2] * kDeg);
	}

	void UpdateHands(RE::PlayerCharacter* a_player)
	{
		static const RE::BSFixedString kNodes[2] = { "NPC R MagicNode [RMag]", "NPC L MagicNode [LMag]" };
		const auto* state = a_player->AsActorState();
		const bool  drawn = state && state->IsWeaponDrawn();
		for (int hand = 0; hand < 2; ++hand) {
			const int   type = drawn ? HandPot(a_player, hand == 1) : -1;
			const auto* item = type >= 0 ? Forms::Types()[type].item : nullptr;
			const char* mesh = item ? item->GetModel() : nullptr;
			const std::string want = mesh && *mesh ? fmt::format("FirePotsHeld|{}", mesh) : std::string();
			for (const bool firstPerson : { false, true }) {
				auto* root = a_player->Get3D(firstPerson);
				auto* magic = root ? root->GetObjectByName(kNodes[hand]) : nullptr;
				auto* node = magic ? magic->AsNode() : nullptr;
				if (!node) {
					continue;
				}
				bool have = false;
				for (std::uint32_t i = 0; i < node->GetChildren().size();) {
					auto& child = node->GetChildren()[i];
					const char* n = child ? child->name.c_str() : nullptr;
					if (n && std::string_view(n).starts_with("FirePotsHeld|")) {
						if (!have && want == n) {
							have = true;
							PoseHeld(child.get());
						} else {
							node->DetachChildAt(i);  // put away, swapped, or a stale copy
							continue;
						}
					}
					++i;
				}
				if (!have && !want.empty()) {
					if (auto obj = MakeHeld(mesh, want)) {
						PoseHeld(obj.get());
						node->AttachChild(obj.get(), true);
						RE::NiUpdateData ud;
						obj->Update(ud);
					}
				}
			}
		}
	}

	// Something to see where a bomb bursts (a short hazard with a borrowed look; none for some payloads).
	void BurstLook(RE::PlayerCharacter* a_player, const Vec3& a_at, int a_payload)
	{
		auto* base = Forms::burstLookProjectile[std::clamp(a_payload, 0, Bombs::kPayloads - 1)];
		if (!base) {
			return;
		}
		RE::Projectile::ProjectileRot rot{ 1.5707963f * g_pitchSign, 0.0f };
		RE::Projectile::LaunchData    data(base, a_player, ToNi(a_at + Vec3{ 0.0f, 0.0f, 30.0f }), rot);
		data.autoAim = false;
		data.useOrigin = true;
		RE::ProjectileHandle h;
		RE::Projectile::Launch(&h, data);
	}

	// --- Fuse bombs: real physics objects ------------------------------------------------------
	// The bomb item itself is dropped at the hand and thrown with havok (so it bounces, rolls and
	// comes to rest) and can't be picked up. When its fuse runs out it's replaced by the impact
	// version of the same bomb dropped onto that spot, which bursts at once.
	struct Lit
	{
		RE::ObjectRefHandle ref;
		int                 type = 0;
		Vec3                origin;
		Vec3                velocity;
		int                 pushes = 0;  // frames the velocity was applied (twice: the body can join the world a frame late)
		int                 waited = 0;  // frames spent waiting for the body to exist
		float               age = 0.0f;
		float               fuse = 2.0f;  // < 0: no fuse (a bomb being set down)
		Vec3                last;
	};
	std::vector<Lit> g_lit;

	// --- Bombs lying in the world: set down, dropped from the pack, or a fuse bomb still rolling --
	// Any of them bursts when struck (a hit event: weapon, arrow, spell) or caught in another
	// bomb's blast; a tripwire also when anyone (you too) comes within 1.7 m, once it's armed.
	// Picking one up disarms it. Found by scanning around the player every second, so they keep
	// working after a reload or when you come back to them.
	struct WorldBomb
	{
		RE::ObjectRefHandle ref;
		float               age = 0.0f;  // seconds since first seen (tripwires arm after kTripwireArmSeconds)
	};
	std::vector<WorldBomb> g_world;
	struct Pending
	{
		RE::ObjectRefHandle ref;
		float               delay = 0.0f;
	};
	std::vector<Pending> g_toBurst;
	std::mutex           g_hitLock;
	std::vector<RE::ObjectRefHandle> g_hits;  // from the hit event sink (any thread)
	float                g_scanTimer = 0.0f;

	// Which bomb a reference in the world is (-1 = not one; debug bombs don't count).
	int BombTypeOf(const RE::TESObjectREFR* a_ref)
	{
		auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
		if (!base || base->GetFormType() != RE::FormType::Scroll) {
			return -1;
		}
		const auto& types = Forms::Types();
		for (std::size_t i = 0; i < types.size(); ++i) {
			if (types[i].item == base) {
				return types[i].debug ? -1 : static_cast<int>(i);
			}
		}
		return -1;
	}

	void AddWorldBomb(const RE::ObjectRefHandle& a_ref)
	{
		if (std::ranges::none_of(g_world, [&](const WorldBomb& w) { return w.ref == a_ref; })) {
			g_world.push_back({ a_ref, 0.0f });
		}
	}

	void QueueBurst(const RE::ObjectRefHandle& a_ref, float a_delay)
	{
		for (const auto& p : g_toBurst) {
			if (p.ref == a_ref) {
				return;
			}
		}
		g_toBurst.push_back({ a_ref, a_delay });
	}

	void ChainFrom(const Vec3& a_at, float a_radius)
	{
		for (const auto& w : g_world) {
			auto ref = w.ref.get();
			if (ref && !ref->IsDisabled() && !ref->IsDeleted() && (ToVec(ref->GetPosition()) - a_at).Length() <= a_radius + 40.0f) {
				QueueBurst(w.ref, 0.15f);  // a short beat between bursts reads as a chain
			}
		}
	}

	class HitSink final : public RE::BSTEventSink<RE::TESHitEvent>
	{
	public:
		static HitSink* GetSingleton()
		{
			static HitSink sink;
			return &sink;
		}
		RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* a_event, RE::BSTEventSource<RE::TESHitEvent>*) override
		{
			if (a_event && a_event->target && BombTypeOf(a_event->target.get()) >= 0) {
				std::scoped_lock l(g_hitLock);
				g_hits.push_back(a_event->target->GetHandle());
			}
			return RE::BSEventNotifyControl::kContinue;
		}

	private:
		HitSink() = default;
	};

	RE::bhkRigidBody* FindBody(RE::TESObjectREFR* a_ref)
	{
		auto* root = a_ref->Get3D();
		if (!root) {
			return nullptr;
		}
		RE::bhkRigidBody* found = nullptr;
		RE::BSVisit::TraverseScenegraphCollision(root, [&](RE::bhkNiCollisionObject* a_col) {
			if (a_col && a_col->body) {
				found = netimmerse_cast<RE::bhkRigidBody*>(a_col->body.get());
			}
			return found ? RE::BSVisit::BSVisitControl::kStop : RE::BSVisit::BSVisitControl::kContinue;
		});
		return found;
	}

	void Detonate(RE::PlayerCharacter* a_player, int a_type, const Vec3& a_at)
	{
		const auto& lit = Forms::Types()[a_type];
		auto&       impact = Forms::Types()[Bombs::Index(Bombs::kImpact, lit.payload, lit.strength)];
		if (!impact.Ready()) {
			return;
		}
		impact.projectile->data.speed = 300.0f;
		RE::Projectile::ProjectileRot rot{ 1.5707963f * g_pitchSign, 0.0f };  // straight down
		RE::Projectile::LaunchData    data(impact.projectile, a_player, ToNi(a_at + Vec3{ 0.0f, 0.0f, 20.0f }), rot);
		data.autoAim = false;
		data.useOrigin = true;
		RE::ProjectileHandle h;
		RE::Projectile::Launch(&h, data);
		if (h) {
			// it only carries the burst the last few inches: don't let a full-size pot flash up
			if (auto p = h.get()) {
				Shrink(p.get(), true);
			}
			g_scaled.push_back({ h, true });
			Track(h, impact);
		}
		logger::info("{} went off at ({:.0f}, {:.0f}, {:.0f})", lit.name, a_at.x, a_at.y, a_at.z);
	}

	bool ThrowLit(RE::PlayerCharacter* a_player, int a_type, const Vec3& a_origin, const Vec3& a_velocity)
	{
		auto& type = Forms::Types()[a_type];
		auto  ref = a_player->PlaceObjectAtMe(type.item, false);
		if (!ref) {
			return false;
		}
		ref->SetActivationBlocked(true);
		if (std::fabs(Config::projectileScale - 1.0f) > 0.001f) {
			ref->SetScale(Config::projectileScale);
		}
		ref->SetPosition(ToNi(a_origin));
		Lit l;
		l.ref = ref->GetHandle();
		l.type = a_type;
		l.origin = a_origin;
		l.velocity = a_velocity;
		l.fuse = Bombs::FuseSeconds(type.trigger);
		l.last = a_origin;
		g_lit.push_back(l);
		AddWorldBomb(ref->GetHandle());
		return true;
	}

	void UpdateLit(RE::PlayerCharacter* a_player, float a_dt)
	{
		for (auto it = g_lit.begin(); it != g_lit.end();) {
			auto ref = it->ref.get();
			if (!ref || ref->IsDeleted() || ref->IsDisabled()) {
				it = g_lit.erase(it);  // gone some other way (cell unloaded...)
				continue;
			}
			it->age += a_dt;
			it->last = ToVec(ref->GetPosition());
			if (it->pushes < 2) {
				if (auto* body = FindBody(ref.get())) {
					const float s = RE::bhkWorld::GetWorldScale();
					if (it->pushes == 0) {
						RE::hkVector4 pos(it->origin.x * s, it->origin.y * s, it->origin.z * s, 0.0f);
						body->SetPosition(pos);
						body->SetAngularVelocity(RE::hkVector4(4.0f, 1.5f, 0.0f, 0.0f));  // a little tumble
					}
					body->SetLinearVelocity(RE::hkVector4(it->velocity.x * s, it->velocity.y * s, it->velocity.z * s, 0.0f));
					if (++it->pushes == 2 && it->fuse >= 0.0f) {
						logger::info("{} thrown as a physics object ({:.0f} u/s)", Forms::Types()[it->type].name, it->velocity.Length());
					}
				} else if (++it->waited == 60) {
					logger::warn("{}: no physics body (the container's mesh has no collision?); it will just drop",
						Forms::Types()[it->type].name);
				}
			}
			if (it->fuse < 0.0f) {
				if (it->pushes >= 2 || it->waited >= 60) {
					it = g_lit.erase(it);  // set down; from here on it's just a bomb lying there
				} else {
					++it;
				}
				continue;
			}
			if (it->age >= it->fuse) {
				const Vec3 at = it->last;
				const int  type = it->type;
				ref->Disable();
				ref->SetDelete(true);
				it = g_lit.erase(it);
				Detonate(a_player, type, at);
				continue;
			}
			++it;
		}
	}

	// Is anyone (alive) standing within a tripwire's reach?
	bool SomeoneNear(RE::PlayerCharacter* a_player, const Vec3& a_at)
	{
		auto close = [&](RE::Actor* a) {
			if (!a || a->IsDead() || a->IsDisabled()) {
				return false;
			}
			const Vec3 d = ToVec(a->GetPosition()) - a_at;
			return std::sqrt(d.x * d.x + d.y * d.y) <= Bombs::kTripwireRadius && std::fabs(d.z) < 150.0f;
		};
		if (close(a_player)) {
			return true;
		}
		bool found = false;
		if (auto* lists = RE::ProcessLists::GetSingleton()) {
			lists->ForEachHighActor([&](RE::Actor* a_actor) {
				if (close(a_actor)) {
					found = true;
					return RE::BSContainer::ForEachResult::kStop;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}
		return found;
	}

	void UpdateWorld(RE::PlayerCharacter* a_player, float a_dt)
	{
		{
			std::scoped_lock l(g_hitLock);
			for (const auto& h : g_hits) {
				QueueBurst(h, 0.0f);
			}
			g_hits.clear();
		}
		g_scanTimer -= a_dt;
		if (g_scanTimer <= 0.0f) {
			g_scanTimer = 1.0f;
			std::erase_if(g_world, [](const WorldBomb& w) {
				auto r = w.ref.get();
				return !r || r->IsDisabled() || r->IsDeleted() || !r->GetParentCell();
			});
			if (auto* tes = RE::TES::GetSingleton()) {
				tes->ForEachReferenceInRange(a_player, 8000.0f, [](RE::TESObjectREFR* a_ref) {
					if (a_ref && !a_ref->IsDisabled() && BombTypeOf(a_ref) >= 0) {
						AddWorldBomb(a_ref->GetHandle());
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}
		}
		// tripwires: anyone close, once armed
		for (auto& w : g_world) {
			w.age += a_dt;
			if (w.age < Bombs::kTripwireArmSeconds) {
				continue;
			}
			auto ref = w.ref.get();
			const int type = BombTypeOf(ref.get());
			if (type >= 0 && Forms::Types()[type].trigger == Bombs::kTripwire && ref->GetParentCell() &&
				SomeoneNear(a_player, ToVec(ref->GetPosition()))) {
				QueueBurst(w.ref, 0.0f);
			}
		}
		for (auto it = g_toBurst.begin(); it != g_toBurst.end();) {
			it->delay -= a_dt;
			if (it->delay > 0.0f) {
				++it;
				continue;
			}
			auto ref = it->ref.get();
			it = g_toBurst.erase(it);
			const int type = BombTypeOf(ref.get());
			if (!ref || type < 0 || ref->IsDisabled() || ref->IsDeleted() || !ref->GetParentCell()) {
				continue;  // picked up, or already gone
			}
			const Vec3 at = ToVec(ref->GetPosition());
			ref->Disable();
			ref->SetDelete(true);
			Detonate(a_player, type, at);  // its burst chains on to others nearby (Track -> ChainFrom)
		}
	}

	// Sets a bomb down at a spot: it sits there as an item (a fuse bomb's fuse is lit; you can't pick
	// that one up).
	bool PlaceBomb(RE::PlayerCharacter* a_player, int a_type, const Vec3& a_at)
	{
		auto& type = Forms::Types()[a_type];
		auto  ref = a_player->PlaceObjectAtMe(type.item, false);
		if (!ref) {
			return false;
		}
		const bool lit = Bombs::IsFuse(type.trigger);
		if (lit) {
			ref->SetActivationBlocked(true);
		}
		if (std::fabs(Config::projectileScale - 1.0f) > 0.001f) {
			ref->SetScale(Config::projectileScale);
		}
		const Vec3 at = a_at + Vec3{ 0.0f, 0.0f, 6.0f };
		ref->SetPosition(ToNi(at));
		Lit l;  // pins it to the spot once its physics body exists
		l.ref = ref->GetHandle();
		l.type = a_type;
		l.origin = at;
		l.fuse = lit ? Bombs::FuseSeconds(type.trigger) : -1.0f;
		l.last = at;
		g_lit.push_back(l);
		AddWorldBomb(ref->GetHandle());
		return true;
	}

	// --- Aim -------------------------------------------------------------------------------
	struct Aim
	{
		Vec3 origin;
		Vec3 forward;  // camera forward (where the crosshair points)
		Vec3 camera;   // camera position
	};

	std::optional<Aim> GetAim(RE::PlayerCharacter* a_player, bool a_leftHand)
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
		aim.camera = camPos;

		const bool firstPerson = camera->IsInFirstPerson();
		RE::NiAVObject* hand = nullptr;
		if (auto* root = a_player->Get3D(firstPerson)) {
			const auto& node = a_leftHand ? Config::throwNode : Config::throwNodeRight;
			hand = root->GetObjectByName(RE::BSFixedString(node.c_str()));
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
		bool               place = false;  // tripwire: set down at placePoint instead of thrown
		Vec3               placePoint;
	};

	RayCtx MakeRayCtx(RE::PlayerCharacter* a_player)
	{
		auto*  cell = a_player->GetParentCell();
		RayCtx ctx;
		ctx.world = cell ? cell->GetbhkWorld() : nullptr;
		if (ctx.world) {
			RE::CFilter own{};
			a_player->GetCollisionFilterInfo(own);
			// projectile layer, in the player's own collision group so the ray ignores the player
			ctx.filter = (own.filter & 0xFFFF0000u) | static_cast<std::uint32_t>(RE::COL_LAYER::kProjectile);
		}
		return ctx;
	}

	// Where a tripwire goes: the ground under the crosshair, at most placeReach ahead.
	Vec3 PlacePoint(RE::PlayerCharacter* a_player, const Aim& a_aim)
	{
		auto        ctx = MakeRayCtx(a_player);
		const float reach = Config::placeReach + 150.0f;  // the camera sits behind the player in third person
		Vec3        point = a_aim.camera + a_aim.forward * reach;
		float       frac = 1.0f;
		Vec3        normal;
		if (ctx.world && CastRay(ctx, a_aim.camera, point, frac, normal)) {
			point = a_aim.camera + a_aim.forward * (reach * frac);
		}
		// keep it within reach of the player, then drop it to the ground
		const Vec3 feet = ToVec(a_player->GetPosition());
		Vec3       flat = point - feet;
		flat.z = 0.0f;
		if (flat.Length() > Config::placeReach) {
			flat = flat.Normalized() * Config::placeReach;
		}
		point = Vec3{ feet.x + flat.x, feet.y + flat.y, (std::max)(point.z, feet.z) + 40.0f };
		const Vec3 below = point + Vec3{ 0.0f, 0.0f, -600.0f };
		if (ctx.world && CastRay(ctx, point, below, frac, normal)) {
			point = point + (below - point) * frac;
		} else {
			point.z = feet.z;
		}
		return point;
	}

	Plan MakePlan(RE::PlayerCharacter* a_player, const Aim& a_aim, float a_charge, bool a_setDown = false)
	{
		Plan p;
		if (a_setDown || Bombs::IsPlaced(CurrentType().trigger)) {
			p.place = true;
			p.placePoint = PlacePoint(a_player, a_aim);
			p.speed = 30.0f;
			p.dir = { 0.0f, 0.0f, -1.0f };
			p.launch.origin = p.placePoint + Vec3{ 0.0f, 0.0f, 12.0f };
			p.launch.velocity = p.dir * p.speed;
			p.launch.gravity = PreviewGravity();
			return p;
		}
		p.speed = Ballistics::ChargeToSpeed(a_charge, Config::minSpeed, Config::maxSpeed);
		p.dir = Ballistics::Loft(a_aim.forward, Config::loftDegrees * Ballistics::kPi / 180.0f);
		p.launch.origin = a_aim.origin;
		p.launch.velocity = p.dir * p.speed;
		// fuse bombs fly as havok objects: real-world gravity
		p.launch.gravity = Bombs::IsFuse(CurrentType().trigger) ? Ballistics::kDefaultGravityUnits : PreviewGravity();
		return p;
	}

	void PublishPreview(RE::PlayerCharacter* a_player, const Plan& a_plan, float a_charge, int a_pots)
	{
		ArcState::Frame frame;
		frame.potName = CurrentType().name;
		frame.color = CurrentType().payload;
		frame.active = true;
		frame.charge = a_charge;
		frame.speed = a_plan.speed;
		frame.potsLeft = a_pots;

		auto* camera = RE::Main::WorldRootCamera();
		if (!Config::showArc || !camera) {
			ArcState::Publish(std::move(frame));  // the meter only
			return;
		}
		if (a_plan.place) {
			// tripwire: a ring where it will sit, as wide as its trigger distance
			frame.landed = true;
			frame.path.push_back(Project(camera, a_plan.placePoint));
			for (const auto& p : Ballistics::Ring(a_plan.placePoint + Vec3{ 0.0f, 0.0f, 4.0f }, Vec3{ 0.0f, 0.0f, 1.0f }, CurrentType().trigger == Bombs::kTripwire ? Bombs::kTripwireRadius : Bombs::BlastRadius(CurrentType().payload, CurrentType().strength), 32)) {
				frame.ring.push_back(Project(camera, p));
			}
			ArcState::Publish(std::move(frame));
			return;
		}
		auto ctx = MakeRayCtx(a_player);
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

		frame.path.reserve(arc.points.size());
		for (const auto& p : arc.points) {
			frame.path.push_back(Project(camera, p));
		}
		if (arc.hit) {
			const float r = Bombs::BlastRadius(CurrentType().payload, CurrentType().strength);
			for (const auto& p : Ballistics::Ring(arc.hitPoint, arc.hitNormal, r, 32)) {
				frame.ring.push_back(Project(camera, p));
			}
		}
		ArcState::Publish(std::move(frame));
	}

	// --- The throw -------------------------------------------------------------------------
	// a_takeItem: false when vanilla already used the scroll up (thrown from a hand by casting it)
	void Launch(RE::PlayerCharacter* a_player, const Plan& a_plan, float a_charge, bool a_takeItem = true)
	{
		auto& type = CurrentType();
		if (a_plan.place && !type.debug) {
			if (!PlaceBomb(a_player, static_cast<int>(g_typeIndex), a_plan.placePoint)) {
				Report("Couldn't set the bomb down (see FirePots.log)", true);
				return;
			}
			if (a_takeItem) {
				a_player->RemoveItem(type.item, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			}
			logger::info("Set down a {} at ({:.0f}, {:.0f}, {:.0f})", type.name, a_plan.placePoint.x, a_plan.placePoint.y, a_plan.placePoint.z);
			return;
		}
		if (Bombs::IsFuse(type.trigger)) {
			// start a little ahead of the hand so it doesn't knock into the thrower
			const Vec3 origin = a_plan.launch.origin + a_plan.dir * 25.0f;
			if (!ThrowLit(a_player, static_cast<int>(g_typeIndex), origin, a_plan.launch.velocity)) {
				Report("Bomb throw failed (see FirePots.log)", true);
				logger::error("PlaceObjectAtMe returned nothing for {}", type.name);
				return;
			}
			if (a_takeItem) {
				a_player->RemoveItem(type.item, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			}
			logger::info("Threw a {}: {:.0f}% wind-up, {:.0f} u/s, pitch {:.1f} deg, fuse {:.1f} s", type.name, a_charge * 100.0f,
				a_plan.speed, std::asin(a_plan.dir.z) * 180.0f / Ballistics::kPi, Bombs::FuseSeconds(type.trigger));
			return;
		}
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
			Report("Bomb launch failed (see FirePots.log)", true);
			logger::error("Projectile::Launch returned no projectile");
			return;
		}
		if (a_takeItem) {
			a_player->RemoveItem(type.item, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
		}
		if (std::fabs(Config::projectileScale - 1.0f) > 0.001f) {
			if (auto p = handle.get()) {
				p->SetScale(Config::projectileScale);
				Shrink(p.get());
			}
			g_scaled.push_back({ handle, false });
		}

		Track(handle, type);
		if (type.trigger == Bombs::kImpact && (Config::autoCalibrate || g_gravityPerUnit <= 0.0f)) {
			g_flight = Flight{};
			g_flight.active = true;
			g_flight.handle = handle;
			g_flight.baseSpeedSet = base->data.speed;
			g_flight.baseGravity = base->data.gravity;
			g_flight.dir = a_plan.dir;
		}
		logger::info("{} a {}: {:.0f}% wind-up, {:.0f} u/s, pitch {:.1f} deg, from ({:.0f}, {:.0f}, {:.0f})",
			a_plan.place ? "Placed" : "Threw", type.name, a_charge * 100.0f, a_plan.speed, std::asin(a_plan.dir.z) * 180.0f / Ballistics::kPi,
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
		int   source = 0;       // what started it: 0 = throw key, 1 = right attack, 2 = left attack
		bool  leftHand = true;  // which hand the pot leaves from
		float held = 0.0f;      // seconds the key has been held
		float cooldown = 0.0f;  // seconds until the next throw is allowed
		int   pots = 0;         // counted when the wind-up starts
	};
	ThrowState g_throw;

	// A pot thrown from a hand is a scroll cast: on release, vanilla plays the hand's release
	// animation and uses the scroll up; when that happens (the count drops) the real pot is launched.
	// Released too early (before the 0.3 s charge), vanilla cancels and nothing is used.
	struct PendingCast
	{
		bool  active = false;
		int   type = 0;
		bool  leftHand = true;
		float charge = 0.0f;
		int   countBefore = 0;
		float waited = 0.0f;
		bool  place = false;  // a quick tap: set it down instead of throwing
	};
	PendingCast g_pending;

	void StopWinding()
	{
		g_throw.phase = Phase::kIdle;
		g_throw.held = 0.0f;
		ArcState::Clear();
	}

	bool IsDown(int a_source)
	{
		return a_source == 0 ? g_input.keyDown.load() : g_input.handDown[a_source - 1].load();
	}

	void Tick(RE::PlayerCharacter* a_player, float)
	{
		// Real time, not the update's delta (it can be scaled, or come more than once a frame).
		static auto last = std::chrono::steady_clock::now();
		const auto  now = std::chrono::steady_clock::now();
		const float dt = std::clamp(std::chrono::duration<float>(now - last).count(), 0.0f, 0.1f);
		last = now;

		if (g_input.reload.exchange(false)) {
			Config::Load();
			Report("Settings reloaded", true);
		}

		TrackFlight(dt);
		UpdateScaled();
		UpdateHands(a_player);
		UpdateLit(a_player, dt);
		UpdateOil(a_player, dt);
		IgniteFromOtherFire(a_player, dt);
		UpdateWorld(a_player, dt);
		g_throw.cooldown = (std::max)(0.0f, g_throw.cooldown - dt);

		if (g_pending.active) {
			g_pending.waited += dt;
			g_typeIndex = static_cast<std::size_t>(g_pending.type);
			if (CountPots(a_player, CurrentType()) < g_pending.countBefore) {
				g_pending.active = false;
				if (const auto aim = GetAim(a_player, g_pending.leftHand)) {
					Launch(a_player, MakePlan(a_player, *aim, g_pending.charge, g_pending.place), g_pending.charge, false);
				}
			} else if (g_pending.place && g_pending.waited > 0.2f) {
				// a tap too short for vanilla to cast the scroll: set it down ourselves
				g_pending.active = false;
				if (const auto aim = GetAim(a_player, g_pending.leftHand)) {
					Launch(a_player, MakePlan(a_player, *aim, 0.0f, true), 0.0f, true);
				}
			} else if (g_pending.waited > 0.75f) {
				g_pending.active = false;  // vanilla cancelled the cast (released too soon, staggered...)
				logger::info("Hand throw cancelled (the scroll wasn't cast)");
			}
		}

		const bool keyPressed = g_input.pressed.exchange(false);
		const bool handPressed[2] = { g_input.handPressed[0].exchange(false), g_input.handPressed[1].exchange(false) };
		const int  potRight = Forms::Ready() ? HandPot(a_player, false) : -1;
		const int  potLeft = Forms::Ready() ? HandPot(a_player, true) : -1;

		if (g_input.craft.exchange(false) && g_throw.phase == Phase::kIdle) {
			auto*      ui = RE::UI::GetSingleton();
			auto*      controls = RE::ControlMap::GetSingleton();
			const bool free = ui && controls && !ui->GameIsPaused() && !ui->IsMenuOpen(RE::Console::MENU_NAME) &&
			                  controls->IsMovementControlsEnabled();  // not in dialogue, cutscenes, other menus
			if (free) {
				Crafting::Open(a_player);
			}
		}

		if (g_input.cycle.exchange(false) && Forms::Ready() && g_throw.phase == Phase::kIdle) {
			if (potLeft >= 0 || potRight >= 0) {
				// a pot in hand: swap it for the next kind you carry
				const bool left = potLeft >= 0;
				const int  held = left ? potLeft : potRight;
				g_typeIndex = static_cast<std::size_t>(held);
				if (SelectCarriedType(a_player, true) && static_cast<int>(g_typeIndex) != held) {
					EquipPot(a_player, static_cast<int>(g_typeIndex), left);
					Report(fmt::format("{} ({})", CurrentType().name, CountPots(a_player, CurrentType())), true);
				} else {
					Report("You have no other kind of bomb.", true);
				}
			} else if (SelectCarriedType(a_player, true)) {
				Report(fmt::format("{} ({})", CurrentType().name, CountPots(a_player, CurrentType())), true);
			} else {
				Report("You have no bombs.", true);
			}
		}

		if (g_throw.phase == Phase::kIdle) {
			// What starts a throw: the throw key, or the attack button of a hand holding a pot
			// (vanilla can't cast the pot: the scroll never finishes charging).
			const auto* state = a_player->AsActorState();
			const bool  drawn = state && state->IsWeaponDrawn();
			int         source = -1;
			if (keyPressed) {
				source = 0;
			} else if (drawn && handPressed[0] && potRight >= 0) {
				source = 1;
			} else if (drawn && handPressed[1] && potLeft >= 0) {
				source = 2;
			}
			if (source < 0) {
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
			if (source == 0 && g_throw.cooldown > 0.0f) {
				return;
			}
			if (source == 1 || source == 2) {
				g_typeIndex = static_cast<std::size_t>(source == 1 ? potRight : potLeft);
				g_throw.leftHand = source == 2;
			} else if (potLeft >= 0 || potRight >= 0) {
				// throw key with a pot in hand: throw that one, from that hand
				g_throw.leftHand = potLeft >= 0;
				g_typeIndex = static_cast<std::size_t>(g_throw.leftHand ? potLeft : potRight);
			} else {
				// nothing in hand: the selected kind (V); if you're out of it, one you have
				g_throw.leftHand = true;
				const auto before = g_typeIndex;
				if (!SelectCarriedType(a_player, false)) {
					Report("You have no bombs.", true);
					return;
				}
				if (g_typeIndex != before) {
					Report(fmt::format("Switched to {}", CurrentType().name), false);
				}
			}
			g_throw.pots = CountPots(a_player, CurrentType());
			if (g_throw.pots <= 0) {
				return;
			}
			g_throw.source = source;
			g_throw.phase = Phase::kWinding;
			g_throw.held = 0.0f;
		}

		// --- winding up ---
		// Block cancels a throw-key wind-up (unless the left hand is the one holding a pot).
		const bool cancel = Config::cancelOnBlock && g_throw.source == 0 && handPressed[1] && potLeft < 0;
		if (cancel || Blocker(a_player) || g_throw.held > 60.0f) {
			if (cancel) {
				Report("Throw cancelled", false);
			}
			StopWinding();
			return;
		}
		g_throw.held += dt;
		const float charge = std::clamp(g_throw.held / Config::chargeSeconds, 0.0f, 1.0f);

		const auto aim = GetAim(a_player, g_throw.leftHand);
		if (!aim) {
			StopWinding();
			return;
		}
		const Plan plan = MakePlan(a_player, *aim, charge);

		if (!IsDown(g_throw.source)) {
			if (g_throw.source != 0) {
				// from a hand: vanilla casts the scroll now (with its release animation); launch then
				const bool tap = g_throw.held * 1000.0f < Config::minHoldMs;
				g_pending = { true, static_cast<int>(g_typeIndex), g_throw.leftHand, charge, g_throw.pots, 0.0f, tap };
				StopWinding();
				return;
			}
			// released: a quick tap sets the bomb down where you're looking; a held one is thrown
			if (g_throw.held * 1000.0f < Config::minHoldMs) {
				Launch(a_player, MakePlan(a_player, *aim, 0.0f, true), 0.0f);
				g_throw.cooldown = Config::cooldownMs / 1000.0f;
			} else {
				Launch(a_player, plan, charge);
				g_throw.cooldown = Config::cooldownMs / 1000.0f;
			}
			StopWinding();
			return;
		}

		if (Config::showArc || Config::showChargeMeter) {
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
		g_pending = PendingCast{};
		g_live.clear();
		g_slicks.clear();
		g_lit.clear();
		g_scaled.clear();
		g_world.clear();
		g_toBurst.clear();
		{
			std::scoped_lock l(g_hitLock);
			g_hits.clear();
		}
		g_scanTimer = 0.0f;
		g_input.keyDown = false;
		g_input.pressed = false;
		g_input.cycle = false;
		g_input.craft = false;
		for (int h = 0; h < 2; ++h) {
			g_input.handDown[h] = false;
			g_input.handPressed[h] = false;
		}
	}

	void RegisterEvents()
	{
		if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
			events->AddEventSink<RE::TESHitEvent>(HitSink::GetSingleton());
			logger::info("Watching hits (volatile bombs)");
		}
	}

	void GiveDebugBombs()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!Config::giveDebugSmokeBombs || !player) {
			return;
		}
		int given = 0;
		for (int k = 0; k < Bombs::kDebugSmokes; ++k) {
			const auto& d = Forms::Types()[Forms::DebugIndex(k)];
			if (!d.Ready()) {
				continue;
			}
			if (const int have = CountItem(player, d.item); have < 5) {
				player->AddObjectToContainer(d.item, nullptr, 5 - have, nullptr);
				given += 5 - have;
			}
		}
		if (given > 0) {
			Report(fmt::format("Debug smoke bombs topped up ({} added; Scrolls tab)", given), false);
		}
	}
}
