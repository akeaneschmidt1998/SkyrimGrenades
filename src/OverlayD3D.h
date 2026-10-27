#pragma once
// D3D11 + ImGui side of the throw preview. Kept free of CommonLib types because
// CommonLibSSE-NG's own WinAPI declarations clash with <Windows.h>; OverlayD3D.cpp is the
// only file that includes real Windows/D3D headers.

#include <string>

namespace OverlayD3D
{
	// Pointers are the game's HWND, ID3D11Device*, ID3D11DeviceContext*, IDXGISwapChain*.
	// Hooks IDXGISwapChain::Present and draws the arc every frame while winding up.
	bool Init(void* a_hwnd, void* a_device, void* a_context, void* a_swapChain, std::string& a_error);
}
