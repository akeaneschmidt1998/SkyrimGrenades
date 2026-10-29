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
#include "CraftUI.h"
#include "Config.h"
#include "OverlayD3D.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>

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

	// The bomb-making menu's own state (selections survive closing and reopening).
	CraftUI::State g_menu;
	float          g_cursorX = 0.0f, g_cursorY = 0.0f;
	std::chrono::steady_clock::time_point g_lastFrame = std::chrono::steady_clock::now();

	// Screen size from the swap chain (we don't use the Win32 backend: it would read the real OS
	// cursor, which Skyrim keeps hidden at the centre; the menu has its own cursor).
	ImVec2 ScreenSize()
	{
		DXGI_SWAP_CHAIN_DESC desc{};
		if (SUCCEEDED(g_swapChain->GetDesc(&desc)) && desc.BufferDesc.Width && desc.BufferDesc.Height) {
			return { static_cast<float>(desc.BufferDesc.Width), static_cast<float>(desc.BufferDesc.Height) };
		}
		return { 1920.0f, 1080.0f };
	}

	// Feeds the input the game side collected into ImGui (menu open only).
	void FeedMenuInput(ImGuiIO& io, const ImVec2& a_size)
	{
		std::vector<CraftShared::InputEvent> events;
		{
			std::scoped_lock l(CraftShared::lock);
			events.swap(CraftShared::events);
		}
		using K = CraftShared::InputEvent::Kind;
		for (const auto& e : events) {
			switch (e.kind) {
			case K::kMouseDelta:
				g_cursorX = std::clamp(g_cursorX + e.a * CraftShared::mouseSensitivity, 0.0f, a_size.x - 1.0f);
				g_cursorY = std::clamp(g_cursorY + e.b * CraftShared::mouseSensitivity, 0.0f, a_size.y - 1.0f);
				io.AddMousePosEvent(g_cursorX, g_cursorY);
				break;
			case K::kMouseButton:
				io.AddMouseButtonEvent(e.key, e.down);
				break;
			case K::kWheel:
				io.AddMouseWheelEvent(0.0f, e.a);
				break;
			case K::kKey:
				io.AddKeyEvent(static_cast<ImGuiKey>(e.key), e.down);
				break;
			}
		}
	}

	void Render()
	{
		if (!g_imguiReady) {
			return;
		}
		const bool menu = CraftShared::open;
		const auto frame = ArcState::Snapshot();
		const bool arc = frame.active && (Config::showArc || Config::showChargeMeter);
		if (!menu && !arc) {
			return;  // skip the ImGui frame entirely when there's nothing to draw
		}

		auto&      io = ImGui::GetIO();
		const auto now = std::chrono::steady_clock::now();
		io.DeltaTime = std::clamp(std::chrono::duration<float>(now - g_lastFrame).count(), 1.0f / 1000.0f, 0.25f);
		g_lastFrame = now;
		io.DisplaySize = ScreenSize();

		if (menu) {
			if (CraftShared::justOpened.exchange(false)) {
				g_cursorX = io.DisplaySize.x * 0.5f;
				g_cursorY = io.DisplaySize.y * 0.5f;
				io.ClearInputKeys();  // nothing still "held" from last time
				io.ClearInputMouse();
				io.AddMousePosEvent(g_cursorX, g_cursorY);
				g_menu.firstFrame = true;
			}
			io.ConfigFlags &= ~(ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange);
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
			io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
			io.MouseDrawCursor = true;
			FeedMenuInput(io, io.DisplaySize);
		} else {
			io.ConfigFlags |= ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange;
			io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad);
			io.MouseDrawCursor = false;
		}

		ImGui_ImplDX11_NewFrame();
		ImGui::NewFrame();

		if (menu) {
			CraftModel::Model model;
			{
				std::scoped_lock l(CraftShared::lock);
				model = CraftShared::model;
			}
			auto result = CraftUI::Draw(model, g_menu, io.DisplaySize.x, io.DisplaySize.y);
			if (!result.requests.empty()) {
				std::scoped_lock l(CraftShared::lock);
				CraftShared::requests.insert(CraftShared::requests.end(), result.requests.begin(), result.requests.end());
			}
			if (result.close) {
				CraftShared::closeRequested = true;
			}
		} else {
			ArcDraw::Style style;
			style.showArc = Config::showArc;
			style.thickness = Config::arcThickness;
			style.opacity = Config::arcOpacity;
			style.showRing = Config::showBlastRing;
			style.showMeter = Config::showChargeMeter;
			ArcDraw::Draw(ImGui::GetBackgroundDrawList(), io.DisplaySize.x, io.DisplaySize.y, frame, style);
		}

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

		if (menu && CraftShared::onFrame) {
			CraftShared::onFrame();  // lets the game side act on crafting / closing while paused
		}
	}

	HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* a_this, UINT a_syncInterval, UINT a_flags)
	{
		// The vtable is shared by every DXGI swap chain in the process; only draw on the game's.
		if (a_this == g_swapChain) {
			Render();
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
	std::string g_fontNote;

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
		// the menu's font (Barlow Condensed, shipped with the mod); the built-in one if it's missing
		std::error_code ec;
		if (!Config::menuFont.empty() && std::filesystem::is_regular_file(Config::menuFont, ec)) {
			if (ImFont* font = io.Fonts->AddFontFromFileTTF(Config::menuFont.c_str(), 24.0f)) {
				io.FontDefault = font;
				g_fontNote = "menu font: " + Config::menuFont;
			}
		} else {
			g_fontNote = "menu font not found (" + Config::menuFont + "), using the built-in one";
		}

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
