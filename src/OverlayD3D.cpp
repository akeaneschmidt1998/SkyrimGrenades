// The only translation unit with real Windows / D3D headers (no CommonLib, no PCH).
// Same Present-hook approach as DaggerfallCombat's indicator; both mods can run together (each
// patch chains to whatever was in the vtable slot before it).
#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include "ArcDraw.h"
#include "ArcState.h"
#include "Config.h"
#include "OverlayD3D.h"

namespace
{
	using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);

	constexpr std::size_t kPresentIndex = 8;  // IDXGISwapChain vtable: QI, AddRef, Release, 4x IDXGIObject/DeviceSubObject, Present

	PresentFn               g_originalPresent = nullptr;
	IDXGISwapChain*         g_swapChain = nullptr;
	ID3D11Device*           g_device = nullptr;
	ID3D11DeviceContext*    g_context = nullptr;
	ID3D11RenderTargetView* g_rtv = nullptr;
	HWND                    g_hwnd = nullptr;
	bool                    g_imguiReady = false;

	bool CreateRenderTarget()
	{
		ID3D11Texture2D* backBuffer = nullptr;
		if (FAILED(g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer))) || !backBuffer) {
			return false;
		}
		const HRESULT hr = g_device->CreateRenderTargetView(backBuffer, nullptr, &g_rtv);
		backBuffer->Release();
		return SUCCEEDED(hr);
	}

	void RenderArc()
	{
		if (!g_imguiReady || (!Config::showArc && !Config::showChargeMeter)) {
			return;
		}
		const auto frame = ArcState::Snapshot();
		if (!frame.active) {
			return;  // skip the ImGui frame entirely when not winding up
		}

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		ArcDraw::Style style;
		style.showArc = Config::showArc;
		style.thickness = Config::arcThickness;
		style.opacity = Config::arcOpacity;
		style.showRing = Config::showBlastRing;
		style.showMeter = Config::showChargeMeter;
		const ImVec2 size = ImGui::GetIO().DisplaySize;
		ArcDraw::Draw(ImGui::GetBackgroundDrawList(), size.x, size.y, frame, style);

		ImGui::Render();

		// Draw onto the back buffer, then put back whatever the game had bound.
		ID3D11RenderTargetView* prevRtv = nullptr;
		ID3D11DepthStencilView* prevDsv = nullptr;
		g_context->OMGetRenderTargets(1, &prevRtv, &prevDsv);
		g_context->OMSetRenderTargets(1, &g_rtv, nullptr);

		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		g_context->OMSetRenderTargets(1, &prevRtv, prevDsv);
		if (prevRtv) prevRtv->Release();
		if (prevDsv) prevDsv->Release();
	}

	HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* a_this, UINT a_syncInterval, UINT a_flags)
	{
		// The vtable is shared by every DXGI swap chain in the process; only draw on the game's.
		if (a_this == g_swapChain) {
			RenderArc();
		}
		return g_originalPresent(a_this, a_syncInterval, a_flags);
	}

	bool PatchVtable(void** a_slot, void* a_newFn, void** a_oldFn)
	{
		DWORD oldProtect = 0;
		if (!VirtualProtect(a_slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
			return false;
		}
		*a_oldFn = *a_slot;
		*a_slot = a_newFn;
		VirtualProtect(a_slot, sizeof(void*), oldProtect, &oldProtect);
		return true;
	}
}

namespace OverlayD3D
{
	bool Init(void* a_hwnd, void* a_device, void* a_context, void* a_swapChain, std::string& a_error)
	{
		g_hwnd = static_cast<HWND>(a_hwnd);
		g_device = static_cast<ID3D11Device*>(a_device);
		g_context = static_cast<ID3D11DeviceContext*>(a_context);
		g_swapChain = static_cast<IDXGISwapChain*>(a_swapChain);

		if (!CreateRenderTarget()) {
			a_error = "could not create a render target view for the back buffer";
			return false;
		}

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		auto& io = ImGui::GetIO();
		io.IniFilename = nullptr;  // don't write imgui.ini into the game folder
		io.LogFilename = nullptr;
		// Draw only: never read the mouse or change the cursor.
		io.ConfigFlags |= ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange;

		ImFontConfig fontCfg;
		fontCfg.SizePixels = 24.0f;
		io.Fonts->AddFontDefaultVector(&fontCfg);

		if (!ImGui_ImplWin32_Init(g_hwnd) || !ImGui_ImplDX11_Init(g_device, g_context)) {
			a_error = "ImGui backend init failed";
			return false;
		}
		g_imguiReady = true;

		void** vtbl = *reinterpret_cast<void***>(g_swapChain);
		if (!PatchVtable(&vtbl[kPresentIndex], reinterpret_cast<void*>(&HookedPresent),
				reinterpret_cast<void**>(&g_originalPresent))) {
			a_error = "VirtualProtect failed on the swap chain vtable";
			g_imguiReady = false;
			return false;
		}
		return true;
	}
}
