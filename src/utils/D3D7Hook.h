#pragma once
#include <windows.h>
#include <ddraw.h>
#include <cstdint>

// Simple DirectDraw7/Direct3D7 present-like hook.
// SC4 uses DirectDraw7 primary surface flipping. We intercept Blt/Flip
// on the primary surface to inject ImGui.
// NOTE: This is a best-effort lightweight hook and may need refinement
// with the actual SC4 rendering flow.
class D3D7Hook {
public:
    using RenderCallback = void(*)(IDirect3DDevice7*, IDirectDrawSurface7*);
    // Set callback to be invoked before/after present-like operations
    static void SetRenderCallback(RenderCallback cb) { s_RenderCallback = cb; }

    static bool Initialize(HWND hGameWindow);
    static void Shutdown();

    static bool IsHookActive() { return s_Active; }
    static HWND GetGameWindow() { return s_hWnd; }
    static IDirect3DDevice7* GetD3DDevice() { return s_pD3DDevice; }
    static IDirectDraw7* GetDDraw() { return s_pDDraw; }
    static IDirectDrawSurface7* GetPrimarySurface() { return s_pPrimary; }

private:
    static bool AcquireInterfaces();
    static bool HookPrimarySurface();

    // Our hooked methods (thunks)
    static HRESULT STDMETHODCALLTYPE FlipHook(IDirectDrawSurface7* This, IDirectDrawSurface7* lpDDSurface, DWORD dwFlags);
    static HRESULT STDMETHODCALLTYPE BltHook(IDirectDrawSurface7* This, LPRECT lpDestRect, IDirectDrawSurface7* lpSrcSurface, LPRECT lpSrcRect, DWORD dwFlags, LPDDBLTFX lpDDBltFx);

    static void Render();

    static HWND s_hWnd;
    static IDirectDraw7* s_pDDraw;
    static IDirect3DDevice7* s_pD3DDevice;
    static IDirectDrawSurface7* s_pPrimary;
    static RenderCallback s_RenderCallback;
    static bool s_Active;

    // Original vtable entries we patch
    static HRESULT (STDMETHODCALLTYPE* s_OrigFlip)(IDirectDrawSurface7*, IDirectDrawSurface7*, DWORD);
    static HRESULT (STDMETHODCALLTYPE* s_OrigBlt)(IDirectDrawSurface7*, LPRECT, IDirectDrawSurface7*, LPRECT, DWORD, LPDDBLTFX);
};
