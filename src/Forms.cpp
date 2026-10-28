#include "Forms.h"

#include "Config.h"

namespace
{
	// "Skyrim.esm|0B9BD2" -> that form. Anything without a '|' is not a form reference.
	RE::TESForm* ParseFormRef(const std::string& a_ref)
	{
		const auto bar = a_ref.find('|');
		if (bar == std::string::npos) {
			return nullptr;
		}
		const std::string plugin = a_ref.substr(0, bar);
		RE::FormID        id = 0;
		try {
			id = static_cast<RE::FormID>(std::stoul(a_ref.substr(bar + 1), nullptr, 16));
		} catch (const std::exception&) {
			logger::warn("Bad form reference '{}' (want Plugin.esm|HEXID)", a_ref);
			return nullptr;
		}
		auto* dh = RE::TESDataHandler::GetSingleton();
		auto* form = dh ? dh->LookupForm(id, plugin) : nullptr;
		if (!form) {
			logger::warn("Form {} not found", a_ref);
		}
		return form;
	}

	// INI value -> mesh path: either a form reference (its model is copied) or a mesh path.
	std::string ResolveModel(const std::string& a_setting, RE::TESForm** a_sourceOut = nullptr)
	{
		if (a_setting.find('|') == std::string::npos) {
			return a_setting;  // a mesh path like "FirePots\FirePot.nif" (relative to Data\Meshes)
		}
		auto* form = ParseFormRef(a_setting);
		if (a_sourceOut) {
			*a_sourceOut = form;
		}
		auto* model = form ? skyrim_cast<RE::TESModel*>(form) : nullptr;
		if (!model || !model->GetModel() || !*model->GetModel()) {
			if (form) {
				logger::warn("{} has no model to borrow", a_setting);
			}
			return {};
		}
		return model->GetModel();
	}

	void SetModelFrom(RE::TESModel* a_target, const std::string& a_setting, const char* a_what, RE::TESForm** a_sourceOut = nullptr)
	{
		const auto mesh = ResolveModel(a_setting, a_sourceOut);
		if (a_target && !mesh.empty()) {
			a_target->SetModel(mesh.c_str());
		}
		logger::info("{} model '{}' (from {})", a_what, mesh, a_setting);
	}
}

namespace Forms
{
	RE::TESForm* Lookup(const std::string& a_ref) { return ParseFormRef(a_ref); }

	std::vector<PotType>& Types()
	{
		static std::vector<PotType> types = [] {
			std::vector<PotType> v(Bombs::kCount + Bombs::kDebugSmokes);
			for (int i = 0; i < Bombs::kCount; ++i) {
				v[i].index = i;
				v[i].trigger = Bombs::TriggerOf(i);
				v[i].payload = Bombs::PayloadOf(i);
				v[i].strength = Bombs::StrengthOf(i);
				v[i].name = Bombs::Name(i);
			}
			for (int k = 0; k < Bombs::kDebugSmokes; ++k) {
				auto& d = v[Bombs::kCount + k];
				d.index = Bombs::kCount + k;
				d.trigger = Bombs::kImpact;
				d.payload = Bombs::kSmoke;
				d.strength = Bombs::kModerate;
				d.name = "Debug Smoke Bomb " + std::to_string(k + 1);
				d.debug = true;
			}
			return v;
		}();
		return types;
	}

	int DebugIndex(int k) { return Bombs::kCount + k; }

	bool Ready()
	{
		return std::ranges::any_of(Types(), [](const PotType& t) { return t.Ready(); });
	}

	namespace
	{
		int Worth(const std::string& a_ref)
		{
			auto* form = ParseFormRef(a_ref);
			return form ? (std::max)(0, form->GetGoldValue()) : -1;
		}

		// Cheapest way to fill one ingredient line: value x count (-1 = none of its forms exist)
		int Cheapest(const Config::Ingredient& a_line)
		{
			int best = -1;
			for (const auto& f : a_line.forms) {
				if (const int v = Worth(f); v >= 0 && (best < 0 || v * a_line.count < best)) {
					best = v * a_line.count;
				}
			}
			return best;
		}
	}

	void SetPrices()
	{
		int container = -1;
		for (const auto& c : Config::containers) {
			if (const int v = Worth(c); v >= 0 && (container < 0 || v < container)) {
				container = v;
			}
		}
		container = (std::max)(container, 0);
		int trigger[Bombs::kT]{};
		for (const auto& line : Config::triggerIngredients) {
			if (line.group >= 0 && line.group < Bombs::kT) {
				trigger[line.group] += (std::max)(Cheapest(line), 0);
			}
		}
		int filling[Bombs::kP];
		std::fill(std::begin(filling), std::end(filling), -1);
		for (const auto& line : Config::fillings) {
			if (line.group >= 0 && line.group < Bombs::kP) {
				if (const int v = Cheapest(line); v >= 0 && (filling[line.group] < 0 || v < filling[line.group])) {
					filling[line.group] = v;
				}
			}
		}
		int set = 0;
		for (auto& t : Types()) {
			if (t.debug || !t.Ready()) {
				continue;
			}
			const int   mult = t.strength + 1;
			const float worth = static_cast<float>(container + (trigger[t.trigger] + (std::max)(filling[t.payload], 0)) * mult);
			t.item->value = (std::max)(1, static_cast<int>(std::lround(worth * Config::valueMult)));
			++set;
		}
		const auto& example = Types()[Bombs::Index(Bombs::kImpact, Bombs::kFire, Bombs::kMild)];
		logger::info("Prices set for {} bombs from their ingredients (x{}); e.g. {}: {} gold", set, Config::valueMult, example.name,
			example.item ? example.item->value : 0);
	}

	bool Load()
	{
		auto* dh = RE::TESDataHandler::GetSingleton();
		if (!dh || !dh->LookupModByName(kPlugin)) {
			logger::error("{} is not loaded: enable it in your load order", kPlugin);
			return false;
		}
		int ready = 0;
		int wanted = 0;
		for (auto& t : Types()) {
			if (t.debug || !Bombs::Available(t.payload)) {
				continue;  // smoke: out for now (the debug smoke bombs are loaded below)
			}
			++wanted;
			t.item = dh->LookupForm<RE::ScrollItem>(Bombs::ItemId(t.index), kPlugin);
			t.projectile = dh->LookupForm<RE::BGSProjectile>(Bombs::ProjectileId(t.index), kPlugin);
			t.explosion = dh->LookupForm<RE::BGSExplosion>(Bombs::ExplosionId(t.payload, t.strength), kPlugin);
			if (!t.Ready()) {
				logger::error("{}: {} is missing forms; is the plugin an old version?", kPlugin, t.name);
				continue;
			}
			if (const char* n = t.item->GetName(); n && *n) {
				t.name = n;
			}
			// item + flying bomb look like their trigger's container
			const auto mesh = ResolveModel(Config::triggerModel[t.trigger]);
			if (!mesh.empty()) {
				t.item->SetModel(mesh.c_str());
				t.projectile->SetModel(mesh.c_str());
			}
			++ready;
		}

		// explosions: one per payload x strength, shared by the triggers
		for (int p = 0; p < Bombs::kPayloads; ++p) {
			if (!Bombs::Available(p)) {
				continue;
			}
			RE::TESForm* source = nullptr;
			const auto   mesh = ResolveModel(Config::explosionModel[p], &source);
			auto*        vanilla = source ? source->As<RE::BGSExplosion>() : nullptr;
			for (int s = 0; s < Bombs::kStrengths; ++s) {
				auto* e = dh->LookupForm<RE::BGSExplosion>(Bombs::ExplosionId(p, s), kPlugin);
				if (!e) {
					continue;
				}
				if (!mesh.empty()) {
					e->SetModel(mesh.c_str());
				}
				if (vanilla && !e->data.light) {
					e->data.light = vanilla->data.light;
				}
			}
			logger::info("{} burst model '{}' (from {})", Bombs::kPayloadNames[p], mesh, Config::explosionModel[p]);
		}
		// debug smoke bombs: each gets one candidate look
		for (int k = 0; k < Bombs::kDebugSmokes; ++k) {
			auto& d = Types()[DebugIndex(k)];
			d.item = dh->LookupForm<RE::ScrollItem>(Bombs::DebugItemId(k), kPlugin);
			d.projectile = dh->LookupForm<RE::BGSProjectile>(Bombs::DebugProjectileId(k), kPlugin);
			d.explosion = dh->LookupForm<RE::BGSExplosion>(Bombs::DebugExplosionId(k), kPlugin);
			auto* hazard = dh->LookupForm<RE::BGSHazard>(Bombs::DebugHazardId(k), kPlugin);
			if (!d.Ready() || !hazard) {
				continue;
			}
			if (const auto mesh = ResolveModel(Config::triggerModel[Bombs::kImpact]); !mesh.empty()) {
				d.item->SetModel(mesh.c_str());
				d.projectile->SetModel(mesh.c_str());
			}
			if (k >= static_cast<int>(Config::debugSmokes.size())) {
				continue;
			}
			const auto&  cand = Config::debugSmokes[k];
			RE::TESForm* source = nullptr;
			const auto   mesh = ResolveModel(cand.form, &source);
			if (auto* ex = source ? source->As<RE::BGSExplosion>() : nullptr) {
				d.explosion->SetModel(mesh.c_str());  // an explosion: its burst (the cloud stays invisible)
				d.explosion->data.light = ex->data.light;
			} else {
				hazard->SetModel(mesh.c_str());  // anything else: the lingering cloud
			}
			d.name = "Debug Smoke " + std::to_string(k + 1) + ": " + cand.label;
			d.item->fullName = d.name.c_str();
			logger::info("{}: '{}' (from {})", d.name, mesh, cand.form);
		}

		oilFireProjectile = dh->LookupForm<RE::BGSProjectile>(Bombs::kOilFireProjectileId, kPlugin);
		if (oilFireProjectile && oilFireProjectile->data.explosionType) {
			SetModelFrom(oilFireProjectile->data.explosionType, Config::oilFireModel, "Burning oil");
		}
		logger::info("{} of {} bombs ready", ready, wanted);
		return ready == wanted;
	}
}
