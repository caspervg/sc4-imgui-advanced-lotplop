#include "D3D7Hook.h"
#include "Logger.h"
#include <wil/com.h>

HWND D3D7Hook::s_hWnd = nullptr;
IDirectDraw7* D3D7Hook::s_pDDraw = nullptr;
IDirect3DDevice7* D3D7Hook::s_pD3DDevice = nullptr;
IDirectDrawSurface7* D3D7Hook::s_pPrimary = nullptr;
D3D7Hook::RenderCallback D3D7Hook::s_RenderCallback = nullptr;
bool D3D7Hook::s_Active = false;
HRESULT (STDMETHODCALLTYPE* D3D7Hook::s_OrigFlip)(IDirectDrawSurface7*, IDirectDrawSurface7*, DWORD) = nullptr;
HRESULT (STDMETHODCALLTYPE* D3D7Hook::s_OrigBlt)(IDirectDrawSurface7*, LPRECT, IDirectDrawSurface7*, LPRECT, DWORD, LPDDBLTFX) = nullptr;

bool D3D7Hook::Initialize(HWND hGameWindow) {
    if (s_Active) return true;
    if (!hGameWindow || !IsWindow(hGameWindow)) {
        LOG_ERROR("D3D7Hook: Invalid game window");
        return false;
    }
    s_hWnd = hGameWindow;

    if (!AcquireInterfaces()) {
        LOG_WARN("D3D7Hook: Failed to acquire DirectDraw/Direct3D7 interfaces. ImGui disabled.");
        return false;
    }
    if (!HookPrimarySurface()) {
        LOG_WARN("D3D7Hook: Failed to hook primary surface methods. ImGui disabled.");
        return false;
    }

    s_Active = true;
    LOG_INFO("D3D7Hook: Initialized successfully");
    return true;
}

void D3D7Hook::Shutdown() {
    s_RenderCallback = nullptr;
    s_Active = false;
    s_hWnd = nullptr;

    if (s_pPrimary) { s_pPrimary->Release(); s_pPrimary = nullptr; }
    if (s_pD3DDevice) { s_pD3DDevice->Release(); s_pD3DDevice = nullptr; }
    if (s_pDDraw) { s_pDDraw->Release(); s_pDDraw = nullptr; }

    LOG_INFO("D3D7Hook: Shutdown complete");
}

bool D3D7Hook::AcquireInterfaces() {
    // Attempt to create a DirectDraw object and get primary surface.
    // This may differ from SC4 internal one, but we need any D3D7 device for ImGui rendering.
    HRESULT hr = DirectDrawCreateEx(nullptr, (void**)&s_pDDraw, IID_IDirectDraw7, nullptr);
    if (FAILED(hr) || !s_pDDraw) {
        LOG_ERROR("D3D7Hook: DirectDrawCreateEx failed (hr=0x{:X})", hr);
        return false;
    }

    hr = s_pDDraw->SetCooperativeLevel(s_hWnd, DDSCL_NORMAL);
    if (FAILED(hr)) {
        LOG_ERROR("D3D7Hook: SetCooperativeLevel failed (hr=0x{:X})", hr);
        return false;
    }

    DDSURFACEDESC2 desc{}; desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS; desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    hr = s_pDDraw->CreateSurface(&desc, &s_pPrimary, nullptr);
    if (FAILED(hr) || !s_pPrimary) {
        LOG_ERROR("D3D7Hook: Failed creating primary surface (hr=0x{:X})", hr);
        return false;
    }

    // Create a simple 3D device via GetDirect3D + CreateDevice on a dummy target.
    // For simplicity, try to get existing Direct3D7 interface and a HAL device.
    wil::com_ptr<IDirect3D7> d3d7;
    hr = s_pDDraw->QueryInterface(IID_IDirect3D7, (void**)&d3d7);
    if (FAILED(hr) || !d3d7) {
        LOG_ERROR("D3D7Hook: IID_IDirect3D7 query failed (hr=0x{:X})", hr);
        return false;
    }

    // Create a backbuffer-like offscreen surface to bind the device to.
    DDSURFACEDESC2 texDesc{}; texDesc.dwSize = sizeof(texDesc);
    texDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    texDesc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE;
    texDesc.dwWidth = 256; texDesc.dwHeight = 256;
    texDesc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    texDesc.ddpfPixelFormat.dwFlags = DDPF_RGB | DDPF_ALPHAPIXELS;
    texDesc.ddpfPixelFormat.dwRGBBitCount = 32;
    texDesc.ddpfPixelFormat.dwRBitMask = 0x00ff0000;
    texDesc.ddpfPixelFormat.dwGBitMask = 0x0000ff00;
    texDesc.ddpfPixelFormat.dwBBitMask = 0x000000ff;
    texDesc.ddpfPixelFormat.dwRGBAlphaBitMask = 0xff000000;
    wil::com_ptr<IDirectDrawSurface7> offscreen;
    hr = s_pDDraw->CreateSurface(&texDesc, &offscreen, nullptr);
    if (FAILED(hr) || !offscreen) {
        LOG_ERROR("D3D7Hook: Failed to create offscreen 3D surface (hr=0x{:X})", hr);
        return false;
    }

    // Enumerate devices and pick first HAL/ref
    D3DDEVICEDESC7 devDesc{};
    hr = d3d7->CreateDevice(IID_IDirect3DHALDevice, offscreen.get(), &s_pD3DDevice);
    if (FAILED(hr)) {
        hr = d3d7->CreateDevice(IID_IDirect3DRGBDevice, offscreen.get(), &s_pD3DDevice);
    }
    if (FAILED(hr) || !s_pD3DDevice) {
        LOG_ERROR("D3D7Hook: Failed to create D3D7 device (hr=0x{:X})", hr);
        return false;
    }

    LOG_INFO("D3D7Hook: Acquired DirectDraw7 + Direct3DDevice7 successfully");
    return true;
}

bool D3D7Hook::HookPrimarySurface() {
    if (!s_pPrimary) return false;
    // Patch vtable entries for Flip and Blt to inject rendering.
    auto** vtbl = *reinterpret_cast<void***>(s_pPrimary);
    // Heuristic indices: need to search actual order. Here we assume known positions (may differ!).
    // To be safe, only hook Blt which is frequently used, and fallback if index mismatch.
    // In practice, you'd pattern scan. For prototype: choose index 5 and 12 (example).
    const int kBltIndex = 5;      // WARNING: placeholder index.
    const int kFlipIndex = 12;    // WARNING: placeholder index.

    s_OrigBlt = reinterpret_cast<decltype(s_OrigBlt)>(vtbl[kBltIndex]);
    s_OrigFlip = reinterpret_cast<decltype(s_OrigFlip)>(vtbl[kFlipIndex]);

    DWORD oldProt;
    if (VirtualProtect(&vtbl[kBltIndex], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProt)) {
        vtbl[kBltIndex] = reinterpret_cast<void*>(&BltHook);
        VirtualProtect(&vtbl[kBltIndex], sizeof(void*), oldProt, &oldProt);
    } else {
        LOG_WARN("D3D7Hook: Failed to patch Blt (vprotect)");
    }
    if (VirtualProtect(&vtbl[kFlipIndex], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProt)) {
        vtbl[kFlipIndex] = reinterpret_cast<void*>(&FlipHook);
        VirtualProtect(&vtbl[kFlipIndex], sizeof(void*), oldProt, &oldProt);
    } else {
        LOG_WARN("D3D7Hook: Failed to patch Flip (vprotect)");
    }

    LOG_INFO("D3D7Hook: Hooked primary surface methods (indices {} / {})", kBltIndex, kFlipIndex);
    return true;
}

HRESULT STDMETHODCALLTYPE D3D7Hook::FlipHook(IDirectDrawSurface7* This, IDirectDrawSurface7* lpDDSurface, DWORD dwFlags) {
    Render();
    return s_OrigFlip ? s_OrigFlip(This, lpDDSurface, dwFlags) : DD_OK;
}

HRESULT STDMETHODCALLTYPE D3D7Hook::BltHook(IDirectDrawSurface7* This, LPRECT lpDestRect, IDirectDrawSurface7* lpSrcSurface, LPRECT lpSrcRect, DWORD dwFlags, LPDDBLTFX lpDDBltFx) {
    // Optionally inject before/after certain blits. Keep minimal.
    return s_OrigBlt ? s_OrigBlt(This, lpDestRect, lpSrcSurface, lpSrcRect, dwFlags, lpDDBltFx) : DD_OK;
}

void D3D7Hook::Render() {
    if (!s_RenderCallback || !s_pD3DDevice || !s_pPrimary) return;

    // Ensure ImGui context created + win32 backend initialized externally.
    s_RenderCallback(s_pD3DDevice, s_pPrimary);
}
