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
#include <unordered_map>
#include <vector>
#include "cRZAutoRefCount.h"
#include "cISCPropertyHolder.h"
#include "../ui/LotConfigEntry.h"

class cISC4City;
class cIGZPersistResourceManager;
struct ID3D11Device;

// Progress callback: stage description, current progress, total steps
typedef void (*LotCacheProgressCallback)(const char* stage, int current, int total);

/**
 * Manages the lot configuration cache, including exemplar loading and icon processing.
 */
class LotCacheManager {
public:
    LotCacheManager();
    ~LotCacheManager();

    // Build the complete cache
    void BuildCache(cISC4City* pCity, cIGZPersistResourceManager* pRM, ID3D11Device* pDevice, LotCacheProgressCallback progressCallback = nullptr);

    // Clear all cached data
    void Clear();

    // Check if cache is ready
    bool IsInitialized() const { return cacheInitialized; }

    // Access the cache
    const std::unordered_map<uint32_t, LotConfigEntry>& GetLotConfigCache() const { return lotConfigCache; }

private:
    // Build exemplar cache
    void BuildExemplarCache(cIGZPersistResourceManager* pRM, LotCacheProgressCallback progressCallback);

    // Build lot configuration cache
    void BuildLotConfigCache(cISC4City* pCity, cIGZPersistResourceManager* pRM, ID3D11Device* pDevice, LotCacheProgressCallback progressCallback);

    // Helper to get cached exemplar by instance ID
    bool GetCachedExemplar(uint32_t instanceID, cRZAutoRefCount<cISCPropertyHolder>& outExemplar);

    // Helper to get cached exemplar by instance ID and type
    bool GetCachedExemplarByType(
        uint32_t instanceID,
        uint32_t exemplarTypePropertyID,
        uint32_t expectedTypeValue,
        cRZAutoRefCount<cISCPropertyHolder>& outExemplar
    );

    std::unordered_map<uint32_t, LotConfigEntry> lotConfigCache;
    std::unordered_map<uint32_t, std::vector<std::pair<uint32_t, cRZAutoRefCount<cISCPropertyHolder>>>> exemplarCache;
    bool cacheInitialized;
};
