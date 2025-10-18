// ...new file...
#include "D3D11Hook.h"
#include "Logger.h"
#include "../vendor/imgui/imgui.h"
#include "../vendor/imgui/imgui_impl_win32.h"

// Forward declaration for ImGui Win32 WndProc handler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

ID3D11Device* D3D11Hook::s_pDevice = nullptr;
ID3D11DeviceContext* D3D11Hook::s_pContext = nullptr;
IDXGISwapChain* D3D11Hook::s_pSwapChain = nullptr;
HWND D3D11Hook::s_hWnd = nullptr;
D3D11Hook::PresentCallback D3D11Hook::s_PresentCallback = nullptr;
bool D3D11Hook::s_bHookActive = false;
uint64_t D3D11Hook::s_FrameCount = 0;
HRESULT (STDMETHODCALLTYPE* D3D11Hook::s_OriginalPresent)(IDXGISwapChain*, UINT, UINT) = nullptr;
WNDPROC D3D11Hook::s_OriginalWndProc = nullptr;

bool D3D11Hook::Initialize(HWND hGameWindow) {
    if (s_bHookActive) {
        LOG_WARN("D3D11Hook: Already initialized");
        return true;
    }

    if (!hGameWindow || !IsWindow(hGameWindow)) {
        LOG_ERROR("D3D11Hook: Invalid window handle provided: 0x{:X}", reinterpret_cast<uintptr_t>(hGameWindow));
        return false;
    }

    s_hWnd = hGameWindow;
    LOG_INFO("D3D11Hook: Initializing with game window: 0x{:X}", reinterpret_cast<uintptr_t>(s_hWnd));
    
    if (InstallPresentHook(hGameWindow)) {
        return InstallWndProcHook();
    }
    return false;
}

bool D3D11Hook::InstallPresentHook(HWND hWnd) {
    LOG_INFO("D3D11Hook: Installing Present hook...");

    // Dummy window class
    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "SC4ImGuiDummyDX11";

    RegisterClassExA(&wc);

    HWND hDummyWnd = CreateWindowExA(
        0,
        wc.lpszClassName,
        "DummyDX11",
        WS_OVERLAPPEDWINDOW,
        0, 0, 1, 1,
        nullptr, nullptr,
        wc.hInstance,
        nullptr
    );

    if (!hDummyWnd) {
        LOG_ERROR("D3D11Hook: Failed to create dummy window (error: {})", GetLastError());
        return false;
    }

    // Swap chain description
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = 100;
    sd.BufferDesc.Height = 100;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hDummyWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    sd.Flags = 0;

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL createdFeatureLevel = D3D_FEATURE_LEVEL_10_0;

    IDXGISwapChain* pTempSwapChain = nullptr;
    ID3D11Device* pTempDevice = nullptr;
    ID3D11DeviceContext* pTempContext = nullptr;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        featureLevels,
        _countof(featureLevels),
        D3D11_SDK_VERSION,
        &sd,
        &pTempSwapChain,
        &pTempDevice,
        &createdFeatureLevel,
        &pTempContext
    );

    DestroyWindow(hDummyWnd);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);

    if (FAILED(hr)) {
        LOG_ERROR("D3D11Hook: Failed to create temporary device (HRESULT: 0x{:X})", hr);
        LOG_INFO("D3D11Hook: This is expected if dgVoodoo is not exposing DX11");
        LOG_INFO("D3D11Hook: ImGui will not be available, but plugin will continue normally");
        return false;
    }

    LOG_INFO("D3D11Hook: Temporary device created (FeatureLevel: 0x{:X})", static_cast<int>(createdFeatureLevel));

    // VTable index 8 is Present for IDXGISwapChain
    uintptr_t* pVTable = *reinterpret_cast<uintptr_t**>(pTempSwapChain);
    s_OriginalPresent = reinterpret_cast<decltype(s_OriginalPresent)>(pVTable[8]);
    LOG_INFO("D3D11Hook: Found Present at 0x{:X}", reinterpret_cast<uintptr_t>(s_OriginalPresent));

    DWORD oldProtect;
    if (!VirtualProtect(&pVTable[8], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        LOG_ERROR("D3D11Hook: Failed to change memory protection (error: {})", GetLastError());
        pTempSwapChain->Release();
        pTempDevice->Release();
        pTempContext->Release();
        return false;
    }

    pVTable[8] = reinterpret_cast<uintptr_t>(&PresentHook);
    VirtualProtect(&pVTable[8], sizeof(void*), oldProtect, &oldProtect);

    // Clean up temp objects
    pTempSwapChain->Release();
    pTempDevice->Release();
    pTempContext->Release();

    s_bHookActive = true;
    LOG_INFO("D3D11Hook: Present hook installed successfully");
    return true;
}

HRESULT STDMETHODCALLTYPE D3D11Hook::PresentHook(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    s_FrameCount++;

    if (!s_pDevice && pSwapChain) {
        ID3D11Device* pDevice = nullptr;
        HRESULT hr = pSwapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&pDevice));
        if (SUCCEEDED(hr) && pDevice) {
            s_pDevice = pDevice;
            pDevice->GetImmediateContext(&s_pContext);
            s_pSwapChain = pSwapChain;
            LOG_INFO("D3D11Hook: Captured real game device: 0x{:X}", reinterpret_cast<uintptr_t>(s_pDevice));
            LOG_INFO("D3D11Hook: Captured immediate context: 0x{:X}", reinterpret_cast<uintptr_t>(s_pContext));
            LOG_INFO("D3D11Hook: Captured swap chain: 0x{:X}", reinterpret_cast<uintptr_t>(s_pSwapChain));
        } else {
            LOG_WARN("D3D11Hook: Failed to get device from swap chain (hr=0x{:X})", hr);
        }
    }

    if (s_PresentCallback && s_pDevice && s_pContext) {
        s_PresentCallback(s_pDevice, s_pContext, pSwapChain);
    }

    return s_OriginalPresent(pSwapChain, SyncInterval, Flags);
}

bool D3D11Hook::InstallWndProcHook() {
    if (!s_hWnd) {
        LOG_ERROR("D3D11Hook: Cannot install WndProc hook - no window handle");
        return false;
    }

    // Get the current window procedure
    s_OriginalWndProc = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(s_hWnd, GWLP_WNDPROC));
    if (!s_OriginalWndProc) {
        LOG_ERROR("D3D11Hook: Failed to get original window procedure (error: {})", GetLastError());
        return false;
    }

    // Install our hook
    LONG_PTR result = SetWindowLongPtrW(s_hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WndProcHook));
    if (result == 0) {
        LOG_ERROR("D3D11Hook: Failed to install window procedure hook (error: {})", GetLastError());
        return false;
    }

    LOG_INFO("D3D11Hook: Window procedure hook installed successfully");
    LOG_INFO("D3D11Hook: Original WndProc: 0x{:X}", reinterpret_cast<uintptr_t>(s_OriginalWndProc));
    return true;
}

LRESULT CALLBACK D3D11Hook::WndProcHook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // If ImGui context is not available, bypass ImGui handling entirely
    if (ImGui::GetCurrentContext() == nullptr) {
        return CallWindowProcW(s_OriginalWndProc, hWnd, msg, wParam, lParam);
    }

    // Let ImGui handle the message first
    LRESULT imguiResult = ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
    if (imguiResult) {
        return imguiResult; // ImGui consumed the message
    }

    if (msg == WM_MOUSEACTIVATE) {
        // Check if mouse is over any ImGui window
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse) {
            return 0; // Don't pass click to game when ImGui wants mouse input
        }
    }

    // For scroll wheel messages, check if ImGui wants to handle them
    if (msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL) {
        // Check if mouse is over any ImGui window
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse) {
            return 0; // Don't pass scroll to game when ImGui wants mouse input
        }
    }

    // For keyboard messages, check if ImGui wants to handle them
    if ((msg >= WM_KEYFIRST && msg <= WM_KEYLAST) || msg == WM_CHAR) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureKeyboard) {
            return 0; // Don't pass keyboard input to game when ImGui wants it
        }
    }

    // Pass through to the original window procedure
    return CallWindowProcW(s_OriginalWndProc, hWnd, msg, wParam, lParam);
}

void D3D11Hook::Shutdown() {
    // Restore original window procedure if we hooked it
    if (s_hWnd && s_OriginalWndProc) {
        SetWindowLongPtrW(s_hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(s_OriginalWndProc));
        s_OriginalWndProc = nullptr;
        LOG_INFO("D3D11Hook: Restored original window procedure");
    }

    s_PresentCallback = nullptr;
    s_bHookActive = false;
    s_pDevice = nullptr;      // We don't own device/context
    s_pContext = nullptr;
    s_pSwapChain = nullptr;
    s_hWnd = nullptr;
    s_FrameCount = 0;
    LOG_INFO("D3D11Hook: Shutdown complete");
}

