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

#include <cstdint>

class cISC4City;
class LotCacheManager;
class AdvancedLotPlopUI;
struct ID3D11Device;

/**
 * @brief Orchestrates incremental lot cache building with UI feedback
 *
 * Similar to PropCacheBuildOrchestrator but handles incremental updates.
 * This class manages the state machine for building the lot cache:
 * 1. BuildingExemplarCache - Fast synchronous phase (1-2 frames)
 * 2. BuildingLotConfigCache - Incremental processing (20 lots/frame)
 * 3. Complete - Finalization
 *
 * Call StartBuildCache() to begin, then Update() every frame until IsBuilding() returns false.
 */
class LotCacheBuildOrchestrator {
public:
    /**
     * @brief Construct orchestrator with references to cache manager and UI
     * @param cacheManager The lot cache manager to build
     * @param ui The UI to show progress updates
     */
    LotCacheBuildOrchestrator(LotCacheManager& cacheManager, AdvancedLotPlopUI& ui);

    /**
     * @brief Start the incremental cache build process
     * @param pCity The city instance
     * @param pDevice D3D11 device for thumbnail generation
     * @return true if started successfully, false otherwise
     */
    bool StartBuildCache(cISC4City* pCity, ID3D11Device* pDevice);

    /**
     * @brief Update the cache build (call once per frame until complete)
     * @param pDevice D3D11 device for thumbnail generation
     * @return true if still building, false if complete
     */
    bool Update(ID3D11Device* pDevice);

    /**
     * @brief Cancel the ongoing cache build
     */
    void Cancel();

    /**
     * @brief Check if cache is currently being built
     */
    bool IsBuilding() const { return isBuilding; }

private:
    LotCacheManager& cacheManager;
    AdvancedLotPlopUI& ui;

    bool isBuilding;
    enum class Phase {
        NotStarted,
        BuildingExemplarCache,
        BuildingLotConfigCache,
        Complete
    };
    Phase phase;
    cISC4City* pCity;

    static constexpr int LOTS_PER_FRAME = 20;
};