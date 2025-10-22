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
#pragma once

#include <functional>
#include <d3d11.h>

struct IDXGISwapChain;
class cISC4View3DWin;
class cIGZMessageServer2;

/**
 * @brief Manages ImGui window lifecycle and DirectX integration
 *
 * Responsibilities:
 * - ImGui context initialization/shutdown
 * - DirectX 11 hook setup
 * - Window toggle management
 * - Hotkey registration/unregistration
 * - Render coordination
 */
class WindowManager {
public:
    WindowManager();
    ~WindowManager();

    // Initialization
    bool InitializeImGui();
    void ShutdownImGui();

    // Hotkey management
    void RegisterToggleShortcut(cISC4View3DWin* pView3D, cIGZMessageServer2* pMS2, void* pListener);
    void UnregisterToggleShortcut(cIGZMessageServer2* pMS2, void* pListener);

    // State
    bool IsImGuiInitialized() const { return imGuiInitialized; }

    // Static rendering callback (called by D3D11Hook)
    static void SetRenderCallback(std::function<void(ID3D11Device*, ID3D11DeviceContext*, IDXGISwapChain*)> callback);
    static void OnImGuiRender(ID3D11Device* pDevice, ID3D11DeviceContext* pContext, IDXGISwapChain* pSwapChain);

private:
    bool imGuiInitialized;

    // Static render callback for D3D11Hook
    static std::function<void(ID3D11Device*, ID3D11DeviceContext*, IDXGISwapChain*)> sRenderCallback;
};
