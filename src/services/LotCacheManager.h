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
#include <future>
#include <string>
#include <utility>
#include "cRZAutoRefCount.h"
#include "cISCPropertyHolder.h"
#include "../ui/LotConfigEntry.h"

class cISC4City;

/**
 * @brief Manages all caching operations for lot configurations and exemplars
 *
 * Responsibilities:
 * - Building and storing exemplar cache
 * - Building and storing lot configuration cache (with immediate icon loading)
 * - Async cache building operations
 */
class LotCacheManager {
public:
    explicit LotCacheManager(cISC4City* city);
    ~LotCacheManager();

    // Cache building
    void BuildExemplarCache();
    void BuildLotConfigCache();
    void BuildLotConfigCacheAsync(std::function<void()> onComplete);

    // Cache queries
    bool GetCachedExemplar(uint32_t instanceID, cRZAutoRefCount<cISCPropertyHolder>& outExemplar);
    bool GetCachedExemplarByType(
        uint32_t instanceID,
        uint32_t exemplarTypePropertyID,
        uint32_t expectedTypeValue,
        cRZAutoRefCount<cISCPropertyHolder>& outExemplar
    );

    const std::unordered_map<uint32_t, LotConfigEntry>& GetLotConfigCache() const { return lotConfigCache; }
    std::unordered_map<uint32_t, LotConfigEntry>& GetLotConfigCache() { return lotConfigCache; }

    // State management
    bool IsCacheInitialized() const { return cacheInitialized; }
    void SetCacheInitialized(bool initialized) { cacheInitialized = initialized; }
    void Clear();

    // Async operations
    bool IsAsyncBuildInProgress() const;
    void CancelAsyncBuild();

private:
    cISC4City* pCity;

    // Cache storage
    std::unordered_map<uint32_t, LotConfigEntry> lotConfigCache;
    std::unordered_map<uint32_t, std::vector<std::pair<uint32_t, cRZAutoRefCount<cISCPropertyHolder>>>> exemplarCache;

    // State
    bool cacheInitialized;
    std::future<void> buildFuture;
};
