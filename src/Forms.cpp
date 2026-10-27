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
}

namespace Forms
{
	bool Ready() { return pot && projectile && explosion; }

	bool Load()
	{
		auto* dh = RE::TESDataHandler::GetSingleton();
		if (!dh || !dh->LookupModByName(kPlugin)) {
			logger::error("{} is not loaded: enable it in your load order", kPlugin);
			return false;
		}
		pot = dh->LookupForm<RE::TESObjectMISC>(kPotId, kPlugin);
		projectile = dh->LookupForm<RE::BGSProjectile>(kProjectileId, kPlugin);
		explosion = dh->LookupForm<RE::BGSExplosion>(kExplosionId, kPlugin);
		if (!Ready()) {
			logger::error("{}: missing forms (pot {}, projectile {}, explosion {}); is it an old version?",
				kPlugin, static_cast<void*>(pot), static_cast<void*>(projectile), static_cast<void*>(explosion));
			return false;
		}

		// Pot model: the inventory item and the flying projectile share it.
		const auto potMesh = ResolveModel(Config::potModel);
		if (!potMesh.empty()) {
			pot->SetModel(potMesh.c_str());
			projectile->SetModel(potMesh.c_str());
		}
		logger::info("Pot model: '{}' (from {})", potMesh, Config::potModel);

		// Fire burst model and light from a vanilla explosion.
		RE::TESForm* explSource = nullptr;
		const auto   burstMesh = ResolveModel(Config::explosionModel, &explSource);
		if (!burstMesh.empty()) {
			explosion->SetModel(burstMesh.c_str());
		}
		if (auto* vanilla = explSource ? explSource->As<RE::BGSExplosion>() : nullptr) {
			if (!explosion->data.light) {
				explosion->data.light = vanilla->data.light;
			}
		}
		logger::info("Burst model: '{}', light {:08X} (from {})", burstMesh,
			explosion->data.light ? explosion->data.light->GetFormID() : 0u, Config::explosionModel);

		logger::info("Forms ready: pot {:08X}, projectile {:08X}, explosion {:08X} (radius {}, placed {})",
			pot->GetFormID(), projectile->GetFormID(), explosion->GetFormID(), explosion->data.radius,
			explosion->data.impactPlacedObject ? explosion->data.impactPlacedObject->GetFormID() : 0u);
		return true;
	}
}
