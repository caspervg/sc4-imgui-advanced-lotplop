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

#include <Windows.h>

struct ID3D11Device;
struct ID3D11DeviceContext;

/**
 * @brief Manages ImGui initialization, frame lifecycle, and shutdown
 *
 * ImGui integration with SimCity 4 requires two-stage initialization:
 * 1. Win32 backend initialization when game window is available
 * 2. DirectX 11 backend initialization when D3D11 device is available (first Present call)
 *
 * This class encapsulates this complexity and provides a clean interface for
 * the director to use.
 */
class ImGuiLifecycleManager {
public:
    ImGuiLifecycleManager();
    ~ImGuiLifecycleManager() = default;

    /**
     * @brief Initialize ImGui Win32 backend (stage 1)
     * @param hWindow The game window handle
     * @return true if successful, false otherwise
     */
    bool InitializeWin32(HWND hWindow);

    /**
     * @brief Initialize ImGui DirectX 11 backend (stage 2)
     * @param pDevice The D3D11 device
     * @param pContext The D3D11 device context
     * @return true if successful, false otherwise
     */
    bool InitializeDX11(ID3D11Device* pDevice, ID3D11DeviceContext* pContext);

    /**
     * @brief Begin a new ImGui frame
     * Calls ImGui_ImplDX11_NewFrame(), ImGui_ImplWin32_NewFrame(), and ImGui::NewFrame()
     */
    void BeginFrame();

    /**
     * @brief End the current ImGui frame and render
     * Calls ImGui::Render() and ImGui_ImplDX11_RenderDrawData()
     */
    void EndFrame();

    /**
     * @brief Shutdown ImGui and clean up resources
     */
    void Shutdown();

    /**
     * @brief Check if Win32 backend is initialized
     */
    bool IsWin32Initialized() const { return win32Initialized; }

    /**
     * @brief Check if DirectX 11 backend is initialized
     */
    bool IsDX11Initialized() const { return dx11Initialized; }

    /**
     * @brief Check if fully initialized (both stages complete)
     */
    bool IsFullyInitialized() const { return win32Initialized && dx11Initialized; }

private:
    bool win32Initialized;
    bool dx11Initialized;
};