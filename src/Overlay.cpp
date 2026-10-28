#include "Overlay.h"

#include "OverlayD3D.h"

namespace Overlay
{
	void Install()
	{
		static bool installed = false;
		if (installed) {
			return;
		}

		auto* data = RE::BSGraphics::Renderer::GetRendererDataSingleton();
		auto* window = RE::BSGraphics::Renderer::GetCurrentRenderWindow();
		if (!data || !window) {
			logger::warn("Overlay: renderer not ready yet");
			return;
		}
		if (!data->forwarder || !data->context || !window->swapChain || !window->hWnd) {
			logger::warn("Overlay: D3D objects not ready yet");
			return;
		}

		std::string error;
		if (OverlayD3D::Init(window->hWnd, data->forwarder, data->context, window->swapChain, error)) {
			installed = true;
			logger::info("Overlay: installed; {}", OverlayD3D::g_fontNote);
		} else {
			logger::error("Overlay: failed to install ({})", error);
		}
	}
}
