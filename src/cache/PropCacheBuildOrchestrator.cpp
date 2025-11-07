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
#include "PropCacheBuildOrchestrator.h"

#include <d3d11.h>

#include "cIGZPersistResourceManager.h"
#include "cISC4City.h"
#include "GZServPtrs.h"
#include "PropCacheManager.h"
#include "../props/PropPainterUI.h"
#include "../utils/Logger.h"

PropCacheBuildOrchestrator::PropCacheBuildOrchestrator(
    PropCacheManager& cacheManager,
    PropPainterUI& ui)
    : cacheManager(cacheManager)
    , ui(ui)
    , isBuilding(false)
    , phase(Phase::NotStarted)
    , pCity(nullptr)
    , pDevice(nullptr)
    , pContext(nullptr)
{
}

void PropCacheBuildOrchestrator::SetDeviceContext(ID3D11Device* pDevice, ID3D11DeviceContext* pContext) {
    this->pDevice = pDevice;
    this->pContext = pContext;
}

bool PropCacheBuildOrchestrator::StartBuildCache(cISC4City* pCity)
{
    if (isBuilding) {
        LOG_WARN("Prop cache build already in progress");
        return false;
    }

    if (!pCity) {
        LOG_ERROR("Cannot start prop cache build: no city provided");
        return false;
    }

    if (!pDevice) {
        LOG_ERROR("Cannot start prop cache build: no D3D11 device set (call SetDeviceContext first)");
        return false;
    }

    if (!pContext) {
        LOG_ERROR("Cannot start prop cache build: no device context set (call SetDeviceContext first)");
        return false;
    }

    this->pCity = pCity;
    this->isBuilding = true;
    this->phase = Phase::BuildingPropCache;

    LOG_INFO("Starting incremental prop cache build");

    // Show loading UI
    ui.ShowLoadingWindow(true);
    ui.UpdateLoadingProgress("Initializing...", 0, 0);

    // Start incremental build
    if (!cacheManager.BeginIncrementalBuild(pCity)) {
        LOG_ERROR("Failed to begin incremental prop cache build");
        ui.ShowLoadingWindow(false);
        isBuilding = false;
        return false;
    }

    return true;
}

bool PropCacheBuildOrchestrator::Update() {
    if (!isBuilding) {
        return false;
    }

    switch (phase) {
        case Phase::BuildingPropCache: {
            // Process props incrementally (5 per frame)
            cIGZPersistResourceManagerPtr pRM;

            int processed = cacheManager.ProcessPropBatch(pRM, pDevice, pContext, PROPS_PER_FRAME);

            // Update progress in UI
            int current = cacheManager.GetProcessedPropCount();
            int total = cacheManager.GetTotalPropCount();
            ui.UpdateLoadingProgress("Processing props...", current, total);

            // Check if complete
            if (cacheManager.IsProcessingComplete()) {
                phase = Phase::Complete;
                LOG_INFO("Prop cache processing complete");
            }

            return true; // Still building
        }

        case Phase::Complete: {
            // Finalize cache build
            cacheManager.FinalizeIncrementalBuild();
            LOG_INFO("Incremental prop cache build completed with {} props", cacheManager.GetPropCount());

            // Hide loading UI
            ui.ShowLoadingWindow(false);

            // Reset state
            isBuilding = false;
            phase = Phase::NotStarted;
            pCity = nullptr;
            pDevice = nullptr;
            pContext = nullptr;

            return false; // Done
        }

        default:
            return false;
    }
}

void PropCacheBuildOrchestrator::Cancel() {
    if (!isBuilding) {
        return;
    }

    LOG_INFO("Cancelling incremental prop cache build");

    // Hide loading UI
    ui.ShowLoadingWindow(false);

    // Reset state
    isBuilding = false;
    phase = Phase::NotStarted;
    pCity = nullptr;
    pDevice = nullptr;
    pContext = nullptr;
}