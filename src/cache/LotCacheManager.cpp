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
// ReSharper disable CppDFAUnreachableCode
#include "LotCacheManager.h"
#include "../utils/Logger.h"
#include "cISC4City.h"
#include "cISC4LotConfiguration.h"
#include "cISC4LotConfigurationManager.h"
#include "cIGZPersistResourceManager.h"
#include "cIGZPersistResourceKeyList.h"
#include "cGZPersistResourceKey.h"
#include "cISCProperty.h"
#include "cIGZVariant.h"
#include "cRZBaseString.h"
#include "GZServPtrs.h"
#include "../exemplar/ExemplarUtil.h"
#include "../exemplar/IconResourceUtil.h"
#include "../exemplar/PropertyUtil.h"
#include "../gfx/IconLoader.h"
#include <ddraw.h>

LotCacheManager::LotCacheManager()
    : cacheInitialized(false) {
}

LotCacheManager::~LotCacheManager() {
    Clear();
}

void LotCacheManager::Clear() {
    // Release icon SRVs
    for (auto& kv : lotConfigCache) {
        auto& entry = kv.second;
        if (entry.iconSRV) {
            entry.iconSRV->Release();
            entry.iconSRV = nullptr;
        }
    }

    lotConfigCache.clear();
    exemplarCache.clear();
    cacheInitialized = false;
}

void LotCacheManager::BuildCache(cISC4City* pCity, cIGZPersistResourceManager* pRM, IDirectDraw7* pDDraw, LotCacheProgressCallback progressCallback) {
    if (cacheInitialized) return;

    LOG_INFO("Building lot cache...");
    BuildExemplarCache(pRM, progressCallback);
    BuildLotConfigCache(pCity, pRM, pDDraw, progressCallback);
    cacheInitialized = true;
    LOG_INFO("Lot cache built: {} entries", lotConfigCache.size());
}

void LotCacheManager::BuildExemplarCache(cIGZPersistResourceManager* pRM, LotCacheProgressCallback progressCallback) {
    if (!exemplarCache.empty()) return;

    LOG_INFO("Building exemplar cache...");

    if (progressCallback) {
        progressCallback("Loading exemplars...", 0, 0);
    }

    if (!pRM) return;

    constexpr uint32_t kExemplarType = 0x6534284A;

    cRZAutoRefCount<cIGZPersistResourceKeyList> pKeyList;
    uint32_t totalCount = pRM->GetAvailableResourceList(pKeyList.AsPPObj(), nullptr);

    if (totalCount == 0 || !pKeyList) {
        LOG_WARN("Failed to enumerate resources for exemplar cache");
        return;
    }

    LOG_INFO("Filtering {} resources for exemplars...", totalCount);

    uint32_t exemplarCount = 0;
    uint32_t keyListSize = pKeyList->Size();
    for (uint32_t i = 0; i < keyListSize; i++) {
        // Report progress every 1000 items
        if (progressCallback && (i % 1000 == 0 || i == keyListSize - 1)) {
            progressCallback("Loading exemplars...", static_cast<int>(i), static_cast<int>(keyListSize));
        }

        cGZPersistResourceKey key = pKeyList->GetKey(i);

        // Only cache exemplars
        if (key.type != kExemplarType) continue;

        cRZAutoRefCount<cISCPropertyHolder> exemplar;
        if (pRM->GetResource(key, GZIID_cISCPropertyHolder, exemplar.AsPPVoid(), 0, nullptr)) {
            exemplarCache[key.instance].emplace_back(key.group, exemplar);
            exemplarCount++;
        }
    }

    LOG_INFO("Exemplar cache built: {} exemplars across {} unique instance IDs", exemplarCount, exemplarCache.size());
}

void LotCacheManager::BuildLotConfigCache(cISC4City* pCity, cIGZPersistResourceManager* pRM, IDirectDraw7* pDDraw, LotCacheProgressCallback progressCallback) {
    LOG_INFO("Building lot configuration cache...");

    if (progressCallback) {
        progressCallback("Processing lot configurations...", 0, 16 * 16);
    }

    cISC4LotConfigurationManager* pLotConfigMgr = pCity->GetLotConfigurationManager();
    if (!pLotConfigMgr || !pRM) return;

    lotConfigCache.reserve(2048);

    constexpr uint32_t kPropertyExemplarType = 0x00000010;
    constexpr uint32_t kPropertyExemplarTypeBuilding = 0x00000002;

    SC4HashSet<uint32_t> configIdTable{};
    configIdTable.Init(256);

    int processedSizes = 0;
    const int totalSizes = 16 * 16;

    for (uint32_t x = 1; x <= 16; x++) {
        for (uint32_t z = 1; z <= 16; z++) {
            // Report progress for each size
            if (progressCallback) {
                progressCallback("Processing lot configurations...", processedSizes, totalSizes);
            }
            processedSizes++;

            if (pLotConfigMgr->GetLotConfigurationIDsBySize(configIdTable, x, z)) {
                for (const auto it : configIdTable) {
                    uint32_t lotConfigID = it->key;

                    if (lotConfigCache.count(lotConfigID)) continue;

                    cISC4LotConfiguration* pConfig = pLotConfigMgr->GetLotConfiguration(lotConfigID);
                    if (!pConfig) continue;

                    LotConfigEntry entry;
                    entry.id = lotConfigID;

                    cRZAutoRefCount<cISCPropertyHolder> pLotExemplar;
                    if (GetCachedExemplar(lotConfigID, pLotExemplar)) {
                        uint32_t buildingExemplarID = 0;
                        if (GetLotBuildingExemplarID(pLotExemplar, buildingExemplarID)) {
                            cRZAutoRefCount<cISCPropertyHolder> pBuildingExemplar;
                            if (GetCachedExemplarByType(
                                buildingExemplarID,
                                kPropertyExemplarType,
                                kPropertyExemplarTypeBuilding,
                                pBuildingExemplar
                            )) {
                                // Get display name
                                cRZBaseString displayName;
                                if (PropertyUtil::GetDisplayName(pBuildingExemplar, displayName)) {
                                    cRZBaseString techName;
                                    pConfig->GetName(techName);
                                    entry.name.reserve(displayName.Strlen() + techName.Strlen() + 3);
                                    entry.name = displayName.Data();
                                    entry.name += " (";
                                    entry.name += techName.Data();
                                    entry.name += ")";
                                }

                                // Load icon immediately
                                uint32_t iconInstance = 0;
                                if (ExemplarUtil::GetItemIconInstance(pBuildingExemplar, iconInstance)) {
                                    entry.iconInstance = iconInstance;

                                    if (pDDraw) {
                                        IDirectDrawSurface7* surface = nullptr;
                                        int w = 0, h = 0;
                                        if (IconLoader::LoadIconFromPNG(pRM, iconInstance, pDDraw, &surface, &w, &h)) {
                                            entry.iconSRV = surface;
                                            entry.iconWidth = w;
                                            entry.iconHeight = h;
                                        }
                                    }
                                }

                                // Occupant groups
                                constexpr uint32_t kOccupantGroupProperty = 0xAA1DD396;
                                const cISCProperty* prop = pBuildingExemplar->GetProperty(kOccupantGroupProperty);
                                if (prop) {
                                    const cIGZVariant* val = prop->GetPropertyValue();
                                    if (val && val->GetType() == cIGZVariant::Type::Uint32Array) {
                                        uint32_t count = val->GetCount();
                                        const uint32_t* pVals = val->RefUint32();
                                        if (pVals && count > 0) {
                                            for (uint32_t i = 0; i < count; ++i) {
                                                entry.occupantGroups.insert(pVals[i]);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Fallback to technical name
                    if (entry.name.empty()) {
                        cRZBaseString techName;
                        if (pConfig->GetName(techName)) {
                            entry.name = techName.Data();
                        }
                    }

                    pConfig->GetSize(entry.sizeX, entry.sizeZ);
                    entry.minCapacity = pConfig->GetMinBuildingCapacity();
                    entry.maxCapacity = pConfig->GetMaxBuildingCapacity();
                    entry.growthStage = pConfig->GetGrowthStage();

                    lotConfigCache[lotConfigID] = entry;
                }
            }
        }
    }

    LOG_INFO("Lot configuration cache built: {} entries", lotConfigCache.size());
}

bool LotCacheManager::GetCachedExemplar(uint32_t instanceID, cRZAutoRefCount<cISCPropertyHolder>& outExemplar) {
    auto it = exemplarCache.find(instanceID);
    if (it != exemplarCache.end() && !it->second.empty()) {
        outExemplar = it->second[0].second;
        return true;
    }
    return false;
}

bool LotCacheManager::GetCachedExemplarByType(
    uint32_t instanceID,
    uint32_t exemplarTypePropertyID,
    uint32_t expectedTypeValue,
    cRZAutoRefCount<cISCPropertyHolder>& outExemplar
) {
    auto it = exemplarCache.find(instanceID);
    if (it != exemplarCache.end()) {
        for (const auto& pair : it->second) {
            uint32_t actualType = 0;
            const cISCProperty* pProp = pair.second->GetProperty(exemplarTypePropertyID);
            if (pProp) {
                const cIGZVariant* pVal = pProp->GetPropertyValue();
                if (pVal && pVal->GetType() == cIGZVariant::Type::Uint32) {
                    pVal->GetValUint32(actualType);
                }
            }

            if (actualType == expectedTypeValue) {
                outExemplar = pair.second;
                return true;
            }
        }
    }
    return false;
}
