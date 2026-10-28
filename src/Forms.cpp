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
			std::vector<PotType> v(Bombs::kCount);
			for (int i = 0; i < Bombs::kCount; ++i) {
				v[i].index = i;
				v[i].trigger = Bombs::TriggerOf(i);
				v[i].payload = Bombs::PayloadOf(i);
				v[i].strength = Bombs::StrengthOf(i);
				v[i].name = Bombs::Name(i);
			}
			return v;
		}();
		return types;
	}

	bool Ready()
	{
		return std::ranges::any_of(Types(), [](const PotType& t) { return t.Ready(); });
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
			if (!Bombs::Available(t.payload)) {
				continue;  // smoke: out for now
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
		oilFireProjectile = dh->LookupForm<RE::BGSProjectile>(Bombs::kOilFireProjectileId, kPlugin);
		if (oilFireProjectile && oilFireProjectile->data.explosionType) {
			SetModelFrom(oilFireProjectile->data.explosionType, Config::oilFireModel, "Burning oil");
		}
		logger::info("{} of {} bombs ready", ready, wanted);
		return ready == wanted;
	}
}
