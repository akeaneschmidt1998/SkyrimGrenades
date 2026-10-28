#include "McmReload.h"

#include "Config.h"

namespace
{
	class JournalCloseSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
			RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (a_event && !a_event->opening && a_event->menuName == RE::JournalMenu::MENU_NAME) {
				Config::Load();
				logger::info("Settings reloaded after the Journal/MCM menu closed");
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};
}

namespace McmReload
{
	void Register()
	{
		static JournalCloseSink sink;
		if (auto* ui = RE::UI::GetSingleton()) {
			ui->AddEventSink<RE::MenuOpenCloseEvent>(&sink);
			logger::info("Watching for MCM changes (Journal menu close)");
		}
	}
}
