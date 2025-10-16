/*
 * DX11 Present hook for sc4-imgui-advanced-lotplop.
 * Creates a temporary DX11 device + swap chain to locate the IDXGISwapChain::Present
 * vtable entry and overwrites it to intercept the game's Present calls when running
 * through dgVoodoo (which exposes DX11).
 */
#pragma once
#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>
#include <cstdint>

class D3D11Hook {
public:
    using PresentCallback = void(*)(ID3D11Device*, ID3D11DeviceContext*, IDXGISwapChain*);

    static bool Initialize(HWND hGameWindow);
    static void Shutdown();

    static bool IsHookActive() { return s_bHookActive; }
    static HWND GetGameWindow() { return s_hWnd; }

    static ID3D11Device* GetDevice() { return s_pDevice; }
    static ID3D11DeviceContext* GetContext() { return s_pContext; }
    static IDXGISwapChain* GetSwapChain() { return s_pSwapChain; }

    static void SetPresentCallback(PresentCallback cb) { s_PresentCallback = cb; }

private:
    static bool InstallPresentHook(HWND hWnd);
    static bool InstallWndProcHook();
    static HRESULT STDMETHODCALLTYPE PresentHook(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
    static LRESULT CALLBACK WndProcHook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    static ID3D11Device* s_pDevice;
    static ID3D11DeviceContext* s_pContext;
    static IDXGISwapChain* s_pSwapChain;
    static HWND s_hWnd;
    static PresentCallback s_PresentCallback;
    static bool s_bHookActive;
    static uint64_t s_FrameCount;

    static HRESULT (STDMETHODCALLTYPE* s_OriginalPresent)(IDXGISwapChain*, UINT, UINT);
    static WNDPROC s_OriginalWndProc;
};

