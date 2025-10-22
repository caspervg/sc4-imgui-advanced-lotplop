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
#include "IconLoadingService.h"
#include "../utils/Logger.h"
#include "../utils/D3D11Hook.h"
#include "../exemplar/ExemplarUtil.h"
#include "../gfx/DX11ImageLoader.h"
#include "cIGZPersistResourceManager.h"
#include "GZServPtrs.h"
#include <d3d11.h>

void IconLoadingService::Clear() {
    iconJobQueue.clear();
}

void IconLoadingService::RequestIcon(uint32_t lotID, std::unordered_map<uint32_t, LotConfigEntry>& lotConfigCache) {
    auto it = lotConfigCache.find(lotID);
    if (it == lotConfigCache.end()) return;

    LotConfigEntry& entry = it->second;
    if (entry.iconSRV || entry.iconRequested) return;

    if (entry.iconInstance == 0) {
        // No icon to load
        entry.iconRequested = true; // Prevent repeated requests
        return;
    }

    entry.iconRequested = true;
    iconJobQueue.push_back(lotID);
}

void IconLoadingService::ProcessIconJobs(
    uint32_t maxJobsPerFrame,
    std::unordered_map<uint32_t, LotConfigEntry>& lotConfigCache,
    std::unordered_map<uint32_t, size_t>& lotEntryIndexByID,
    std::vector<LotConfigEntry>& lotEntries
) {
    if (iconJobQueue.empty() || maxJobsPerFrame == 0) return;

    uint32_t processed = 0;
    while (processed < maxJobsPerFrame && !iconJobQueue.empty()) {
        uint32_t lotID = iconJobQueue.front();
        iconJobQueue.pop_front();

        auto it = lotConfigCache.find(lotID);
        if (it == lotConfigCache.end()) { continue; }

        LotConfigEntry& entry = it->second;
        if (entry.iconSRV) { continue; }

        // Resource manager
        cIGZPersistResourceManagerPtr pRM;
        if (!pRM) { continue; }

        // Use stored icon instance to avoid walking exemplar chain
        if (entry.iconInstance == 0) {
            continue; // No icon for this entry
        }

        std::vector<uint8_t> pngBytes;
        if (!ExemplarUtil::LoadPNGByInstance(pRM, entry.iconInstance, pngBytes) || pngBytes.empty()) {
            continue;
        }

        ID3D11Device* device = D3D11Hook::GetDevice();
        if (!device) { continue; }

        ID3D11ShaderResourceView* srv = nullptr;
        int w = 0, h = 0;
        if (gfx::CreateSRVFromPNGMemory(pngBytes.data(), pngBytes.size(), device, &srv, &w, &h)) {
            // Update cache entry
            entry.iconSRV = srv;
            entry.iconWidth = w;
            entry.iconHeight = h;

            // Propagate to visible list if present
            auto mit = lotEntryIndexByID.find(lotID);
            if (mit != lotEntryIndexByID.end()) {
                size_t idx = mit->second;
                if (idx < lotEntries.size()) {
                    lotEntries[idx].iconSRV = srv;
                    lotEntries[idx].iconWidth = w;
                    lotEntries[idx].iconHeight = h;
                }
            }
        }

        processed++;
    }
}
