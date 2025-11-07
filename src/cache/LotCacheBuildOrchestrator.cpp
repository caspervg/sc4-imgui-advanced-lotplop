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
#include "LotCacheBuildOrchestrator.h"
#include "LotCacheManager.h"
#include "../ui/AdvancedLotPlopUI.h"
#include "cISC4City.h"
#include "cIGZPersistResourceManager.h"
#include "GZServPtrs.h"
#include "../utils/Logger.h"
#include <d3d11.h>

LotCacheBuildOrchestrator::LotCacheBuildOrchestrator(
    LotCacheManager& cacheManager,
    AdvancedLotPlopUI& ui)
    : cacheManager(cacheManager)
    , ui(ui)
    , isBuilding(false)
    , phase(Phase::NotStarted)
    , pCity(nullptr)
{
}

bool LotCacheBuildOrchestrator::StartBuildCache(cISC4City* pCity, ID3D11Device* pDevice) {
    if (isBuilding) {
        LOG_WARN("Lot cache build already in progress");
        return false;
    }

    if (!pCity) {
        LOG_ERROR("Cannot start lot cache build: no city provided");
        return false;
    }

    if (!pDevice) {
        LOG_ERROR("Cannot start lot cache build: no D3D11 device available");
        return false;
    }

    this->pCity = pCity;
    this->isBuilding = true;
    this->phase = Phase::BuildingExemplarCache;

    LOG_INFO("Starting incremental lot cache build");

    // Show loading UI
    ui.ShowLoadingWindow(true);
    ui.SetLoadingProgress("Initializing...", 0, 0);

    return true;
}

bool LotCacheBuildOrchestrator::Update(ID3D11Device* pDevice) {
    if (!isBuilding) {
        return false;
    }

    switch (phase) {
        case Phase::BuildingExemplarCache: {
            // Build exemplar cache synchronously (fast operation: 1-2 frames)
            LOG_INFO("Building exemplar cache...");
            cIGZPersistResourceManagerPtr pRM;

            cacheManager.BeginIncrementalBuild();
            cacheManager.BuildExemplarCacheSync(pRM);

            // Move to next phase
            phase = Phase::BuildingLotConfigCache;
            cacheManager.BeginLotConfigProcessing(pCity);
            LOG_INFO("Exemplar cache complete, starting lot config processing");

            return true; // Still building
        }

        case Phase::BuildingLotConfigCache: {
            // Process lots incrementally (20 per frame)
            cIGZPersistResourceManagerPtr pRM;

            int processed = cacheManager.ProcessLotConfigBatch(pRM, pDevice, LOTS_PER_FRAME);

            // Update progress in UI
            int current = cacheManager.GetProcessedLotCount();
            int total = cacheManager.GetTotalLotCount();
            ui.SetLoadingProgress("Processing lot configurations...", current, total);

            // Check if complete
            if (cacheManager.IsLotConfigProcessingComplete()) {
                phase = Phase::Complete;
                LOG_INFO("Lot config processing complete");
            }

            return true; // Still building
        }

        case Phase::Complete: {
            // Finalize cache build
            cacheManager.FinalizeIncrementalBuild();
            LOG_INFO("Incremental cache build completed");

            // Hide loading UI
            ui.ShowLoadingWindow(false);

            // Reset state
            isBuilding = false;
            phase = Phase::NotStarted;
            pCity = nullptr;

            return false; // Done
        }

        default:
            return false;
    }
}

void LotCacheBuildOrchestrator::Cancel() {
    if (!isBuilding) {
        return;
    }

    LOG_INFO("Cancelling incremental lot cache build");

    // Hide loading UI
    ui.ShowLoadingWindow(false);

    // Reset state
    isBuilding = false;
    phase = Phase::NotStarted;
    pCity = nullptr;
}