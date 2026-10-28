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

	bool LoadType(RE::TESDataHandler* a_dh, Forms::PotType& a_type)
	{
		a_type.item = a_dh->LookupForm<RE::ScrollItem>(a_type.itemId, Forms::kPlugin);
		a_type.projectile = a_dh->LookupForm<RE::BGSProjectile>(a_type.baseId + 1, Forms::kPlugin);
		a_type.explosion = a_dh->LookupForm<RE::BGSExplosion>(a_type.baseId + 2, Forms::kPlugin);
		if (!a_type.Ready()) {
			logger::error("{}: {} forms missing (item {}, projectile {}, explosion {}); is it an old version?",
				Forms::kPlugin, a_type.name, static_cast<void*>(a_type.item), static_cast<void*>(a_type.projectile),
				static_cast<void*>(a_type.explosion));
			return false;
		}

		// Pot model: the inventory item and the flying projectile share it.
		const auto potMesh = ResolveModel(*a_type.potModel);
		if (!potMesh.empty()) {
			a_type.item->SetModel(potMesh.c_str());
			a_type.projectile->SetModel(potMesh.c_str());
		}

		// Burst model and light from a vanilla explosion.
		RE::TESForm* explSource = nullptr;
		const auto   burstMesh = ResolveModel(*a_type.explosionModel, &explSource);
		if (!burstMesh.empty()) {
			a_type.explosion->SetModel(burstMesh.c_str());
		}
		if (auto* vanilla = explSource ? explSource->As<RE::BGSExplosion>() : nullptr) {
			if (!a_type.explosion->data.light) {
				a_type.explosion->data.light = vanilla->data.light;
			}
		}

		logger::info("{} ready: item {:08X}, projectile {:08X}, explosion {:08X} (radius {}, placed {:08X}); "
					 "pot model '{}' (from {}), burst model '{}' (from {})",
			a_type.name, a_type.item->GetFormID(), a_type.projectile->GetFormID(), a_type.explosion->GetFormID(),
			a_type.explosion->data.radius,
			a_type.explosion->data.impactPlacedObject ? a_type.explosion->data.impactPlacedObject->GetFormID() : 0u,
			potMesh, *a_type.potModel, burstMesh, *a_type.explosionModel);
		return true;
	}
}

namespace Forms
{
	RE::TESForm* Lookup(const std::string& a_ref) { return ParseFormRef(a_ref); }

	std::vector<PotType>& Types()
	{
		static std::vector<PotType> types{
			PotType{ "Fire Pot", 0x820, 0x800, &Config::firePotModel, &Config::fireExplosionModel },
			PotType{ "Frost Pot", 0x830, 0x810, &Config::frostPotModel, &Config::frostExplosionModel },
		};
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
		bool all = true;
		for (auto& t : Types()) {
			all = LoadType(dh, t) && all;
		}
		return all;
	}
}
