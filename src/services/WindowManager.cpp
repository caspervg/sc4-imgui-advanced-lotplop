/*
 * This file is part of sc4-imgui-advanced-lotplop, a DLL Plugin for
 * SimCity 4 that offers some extra terrain utilities.
 *
 * Copyright (C) 2025 Casper Van Gheluwe
 *
 * sc4-imgui-advanced-lotplop is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * sc4-imgui-advanced-lotplop is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with sc4-imgui-advanced-lotplop.
 * If not, see <http://www.gnu.org/licenses/>.
 */
#include "WindowManager.h"
#include "../utils/Logger.h"
#include "../utils/D3D11Hook.h"
#include "../../vendor/imgui/imgui_impl_win32.h"
#include "../../vendor/imgui/imgui_impl_dx11.h"
#include "cISC4View3DWin.h"
#include "cIGZMessageServer2.h"
#include "cIGZPersistResourceManager.h"
#include "cIGZWinKeyAcceleratorRes.h"
#include "cGZPersistResourceKey.h"
#include "GZServPtrs.h"
#include <windows.h>
#include <dxgi.h>

// Hotkey/message ID to toggle the ImGui window (unique)
static constexpr uint32_t kToggleLotPlopWindowShortcutID = 0x9F21C3A1;

// Private KeyConfig resource to register our hotkey accelerators
static constexpr uint32_t kKeyConfigType = 0xA2E3D533;
static constexpr uint32_t kKeyConfigGroup = 0x8F1E6D69;
static constexpr uint32_t kKeyConfigInstance = 0x5CBCFBF8;

// Static member initialization
std::function<void(ID3D11Device*, ID3D11DeviceContext*, IDXGISwapChain*)> WindowManager::sRenderCallback = nullptr;

WindowManager::WindowManager()
    : imGuiInitialized(false) {
}

WindowManager::~WindowManager() {
    ShutdownImGui();
}

bool WindowManager::InitializeImGui() {
    if (imGuiInitialized) return true;

    // Find the main SimCity 4 window
    HWND hGameWindow = FindWindowA(nullptr, "SimCity 4");

    if (!hGameWindow) {
        // Try getting the active window as fallback
        hGameWindow = GetActiveWindow();
    }

    if (hGameWindow && IsWindow(hGameWindow)) {
        LOG_INFO("Got game window: 0x{:X}", reinterpret_cast<uintptr_t>(hGameWindow));

        ImGui::CreateContext();
        if (D3D11Hook::Initialize(hGameWindow)) {
            LOG_INFO("D3D11Hook initialized successfully");
            D3D11Hook::SetPresentCallback(WindowManager::OnImGuiRender);
            ImGui_ImplWin32_Init(hGameWindow);
            imGuiInitialized = true;
            return true;
        } else {
            LOG_WARN("D3D11Hook failed - ImGui will not be available");
            ImGui::DestroyContext();
        }
    } else {
        LOG_ERROR("Failed to find SimCity 4 window");
    }

    return false;
}

void WindowManager::ShutdownImGui() {
    if (imGuiInitialized) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        imGuiInitialized = false;
    }
    D3D11Hook::Shutdown();
}

void WindowManager::RegisterToggleShortcut(cISC4View3DWin* pView3D, cIGZMessageServer2* pMS2, void* pListener) {
    if (!pView3D) return;

    cIGZPersistResourceManagerPtr pRM;
    if (pRM) {
        cRZAutoRefCount<cIGZWinKeyAcceleratorRes> pAcceleratorRes;
        const cGZPersistResourceKey key(kKeyConfigType, kKeyConfigGroup, kKeyConfigInstance);
        if (pRM->GetPrivateResource(key, kGZIID_cIGZWinKeyAcceleratorRes, pAcceleratorRes.AsPPVoid(), 0, nullptr)) {
            auto* pAccel = pView3D->GetKeyAccelerator();
            if (pAccel) {
                pAcceleratorRes->RegisterResources(pAccel);
                if (pMS2 && pListener) {
                    pMS2->AddNotification(static_cast<cIGZMessageTarget2*>(pListener), kToggleLotPlopWindowShortcutID);
                }
            }
        }
    }
}

void WindowManager::UnregisterToggleShortcut(cIGZMessageServer2* pMS2, void* pListener) {
    if (pMS2 && pListener) {
        pMS2->RemoveNotification(static_cast<cIGZMessageTarget2*>(pListener), kToggleLotPlopWindowShortcutID);
    }
}

void WindowManager::SetRenderCallback(std::function<void(ID3D11Device*, ID3D11DeviceContext*, IDXGISwapChain*)> callback) {
    sRenderCallback = callback;
}

void WindowManager::OnImGuiRender(
    ID3D11Device* pDevice,
    ID3D11DeviceContext* pContext,
    IDXGISwapChain* pSwapChain
) {
    static bool initialized = false;
    static ID3D11RenderTargetView* pRTV = nullptr;

    // If ImGui context has been destroyed, skip rendering safely
    if (ImGui::GetCurrentContext() == nullptr) {
        return;
    }

    // Lazy initialize renderer backend
    if (!initialized) {
        HWND hWnd = D3D11Hook::GetGameWindow();
        if (hWnd && pDevice && pContext) {
            ImGui_ImplDX11_Init(pDevice, pContext);
            initialized = true;
            LOG_INFO("ImGui DX11 backend initialized in render callback");
        }
    }
    if (!initialized) return;

    // Create RTV once from swap chain
    if (!pRTV && pSwapChain) {
        ID3D11Texture2D* pBackBuffer = nullptr;
        HRESULT hr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pBackBuffer));
        if (SUCCEEDED(hr) && pBackBuffer) {
            pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &pRTV);
            pBackBuffer->Release();
            LOG_INFO("Created DX11 render target view for ImGui");
        }
    }
    if (pRTV) {
        pContext->OMSetRenderTargets(1, &pRTV, nullptr);
    }

    // Frame setup
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Call the registered render callback
    if (sRenderCallback) {
        sRenderCallback(pDevice, pContext, pSwapChain);
    }

    // Render
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}
