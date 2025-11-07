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

#include "CacheBuildOrchestratorBase.h"

class cISC4City;
class PropCacheManager;
class PropPainterUI;
struct ID3D11Device;
struct ID3D11DeviceContext;

/**
 * @brief Orchestrates incremental prop cache building with UI feedback
 *
 * Manages incremental processing of the prop cache across multiple frames.
 * Call StartBuildCache() to begin, then Update() every frame until IsBuilding() returns false.
 */
class PropCacheBuildOrchestrator : public CacheBuildOrchestratorBase {
public:
    /**
     * @brief Construct orchestrator with references to cache manager and UI
     * @param cacheManager The prop cache manager to build
     * @param ui The UI to show progress updates
     */
    PropCacheBuildOrchestrator(PropCacheManager& cacheManager, PropPainterUI& ui);

    /**
     * @brief Set the D3D11 device and context (call once at initialization)
     * @param pDevice D3D11 device for rendering
     * @param pContext D3D11 device context
     */
    void SetDeviceContext(ID3D11Device* pDevice, ID3D11DeviceContext* pContext) override;

    /**
     * @brief Start the incremental cache build process
     * @param pCity The city instance
     * @return true if started successfully, false otherwise
     */
    bool StartBuildCache(cISC4City* pCity) override;

    /**
     * @brief Update the cache build (call once per frame until complete)
     * @return true if still building, false if complete
     */
    bool Update() override;

    /**
     * @brief Cancel the ongoing cache build
     */
    void Cancel() override;

    /**
     * @brief Check if cache is currently being built
     */
    bool IsBuilding() const override { return isBuilding; }

private:
    PropCacheManager& cacheManager;
    PropPainterUI& ui;

    bool isBuilding;
    enum class Phase {
        NotStarted,
        BuildingPropCache,
        Complete
    };
    Phase phase;
    cISC4City* pCity;
    ID3D11Device* pDevice;
    ID3D11DeviceContext* pContext;

    static constexpr int PROPS_PER_FRAME = 5;  // Conservative batch size for prop thumbnail generation
};