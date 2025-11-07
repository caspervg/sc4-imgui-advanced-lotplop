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
{
}

bool PropCacheBuildOrchestrator::BuildCache(cISC4City* pCity, ID3D11Device* pDevice) {
    if (isBuilding) {
        LOG_WARN("Prop cache build already in progress");
        return false;
    }

    if (!pCity) {
        LOG_ERROR("Cannot build prop cache: no city loaded");
        return false;
    }

    if (!pDevice) {
        LOG_ERROR("Cannot build prop cache: no D3D11 device available");
        return false;
    }

    isBuilding = true;
    LOG_INFO("Building prop cache...");

    // Show loading UI
    ui.ShowLoadingWindow(true);
    ui.UpdateLoadingProgress("Initializing...", 0, 0);

    // Get required resources
    cIGZPersistResourceManagerPtr pRM;
    if (!pRM) {
        LOG_ERROR("Failed to get resource manager");
        ui.ShowLoadingWindow(false);
        isBuilding = false;
        return false;
    }

    ID3D11DeviceContext* pContext = nullptr;
    pDevice->GetImmediateContext(&pContext);
    if (!pContext) {
        LOG_ERROR("Failed to get device context");
        ui.ShowLoadingWindow(false);
        isBuilding = false;
        return false;
    }

    // Create progress callback that updates UI
    auto progressCallback = [this](const char* stage, int current, int total) {
        ui.UpdateLoadingProgress(stage, current, total);
    };

    // Build the cache
    const bool success = cacheManager.Initialize(pCity, pRM, pDevice, pContext, progressCallback);

    // Release context
    if (pContext) {
        pContext->Release();
    }

    // Hide loading UI
    ui.ShowLoadingWindow(false);
    isBuilding = false;

    if (success) {
        LOG_INFO("Prop cache built successfully with {} props", cacheManager.GetPropCount());
    } else {
        LOG_ERROR("Failed to build prop cache");
    }

    return success;
}