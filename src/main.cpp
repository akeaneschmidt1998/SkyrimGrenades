#include "BottleReturn.h"
#include "Config.h"
#include "Crafting.h"
#include "Forms.h"
#include "LeveledLists.h"
#include "McmReload.h"
#include "Overlay.h"
#include "Thrower.h"

namespace
{
	void SetupLog()
	{
		auto dir = logger::log_directory();
		if (!dir) {
			return;
		}
		*dir /= "FirePots.log";

		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(dir->string(), true);
		auto log = std::make_shared<spdlog::logger>("global", std::move(sink));
		log->set_level(spdlog::level::debug);
		log->flush_on(spdlog::level::debug);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v");
	}

	void OnSKSEMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kInputLoaded:
			Thrower::RegisterInput();
			Crafting::Register();
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			Forms::Load();
			BottleReturn::Init();
			LeveledLists::Inject();
			McmReload::Register();
			Overlay::Install();
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			Crafting::ForceClose();
			[[fallthrough]];
		case SKSE::MessagingInterface::kPostLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			Thrower::Reset();
			Overlay::Install();  // retry in case the renderer wasn't ready at kDataLoaded
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	SetupLog();

	logger::info("FirePots loading");
	Config::Load();
	Thrower::InstallHooks();

	if (!SKSE::GetMessagingInterface()->RegisterListener(OnSKSEMessage)) {
		logger::critical("Could not register SKSE message listener");
		return false;
	}

	logger::info("FirePots loaded");
	return true;
}
