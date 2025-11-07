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
#include "ImGuiLifecycleManager.h"

#include <d3d11.h>
#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>

#include "Logger.h"

ImGuiLifecycleManager::ImGuiLifecycleManager()
    : win32Initialized(false)
    , dx11Initialized(false) {
}

bool ImGuiLifecycleManager::InitializeWin32(HWND hWindow) {
    if (win32Initialized) {
        LOG_WARN("ImGui Win32 backend already initialized");
        return true;
    }

    if (!hWindow || !IsWindow(hWindow)) {
        LOG_ERROR("Invalid window handle for ImGui Win32 initialization");
        return false;
    }

    // Create ImGui context
    ImGui::CreateContext();
    LOG_INFO("ImGui context created");

    // Initialize Win32 backend
    if (!ImGui_ImplWin32_Init(hWindow)) {
        LOG_ERROR("Failed to initialize ImGui Win32 backend");
        ImGui::DestroyContext();
        return false;
    }

    win32Initialized = true;
    LOG_INFO("ImGui Win32 backend initialized");
    return true;
}

bool ImGuiLifecycleManager::InitializeDX11(ID3D11Device* pDevice, ID3D11DeviceContext* pContext) {
    if (dx11Initialized) {
        LOG_WARN("ImGui DX11 backend already initialized");
        return true;
    }

    if (!win32Initialized) {
        LOG_ERROR("Cannot initialize DX11 backend: Win32 backend not initialized");
        return false;
    }

    if (!pDevice || !pContext) {
        LOG_ERROR("Invalid D3D11 device or context for ImGui DX11 initialization");
        return false;
    }

    // Initialize DX11 backend
    if (!ImGui_ImplDX11_Init(pDevice, pContext)) {
        LOG_ERROR("Failed to initialize ImGui DX11 backend");
        return false;
    }

    dx11Initialized = true;
    LOG_INFO("ImGui DX11 backend initialized");
    return true;
}

void ImGuiLifecycleManager::BeginFrame() {
    if (!IsFullyInitialized()) {
        return;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLifecycleManager::EndFrame() {
    if (!IsFullyInitialized()) {
        return;
    }

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void ImGuiLifecycleManager::Shutdown() {
    if (dx11Initialized) {
        ImGui_ImplDX11_Shutdown();
        dx11Initialized = false;
        LOG_INFO("ImGui DX11 backend shut down");
    }

    if (win32Initialized) {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        win32Initialized = false;
        LOG_INFO("ImGui Win32 backend shut down");
    }
}