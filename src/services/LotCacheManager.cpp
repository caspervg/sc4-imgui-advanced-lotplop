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
#include "LotCacheManager.h"
#include "../utils/Logger.h"
#include "../utils/D3D11Hook.h"
#include "../exemplar/ExemplarUtil.h"
#include "../exemplar/PropertyUtil.h"
#include "../exemplar/IconResourceUtil.h"
#include "../gfx/DX11ImageLoader.h"
#include "cISC4City.h"
#include "cISC4LotConfiguration.h"
#include "cISC4LotConfigurationManager.h"
#include "cIGZPersistResourceManager.h"
#include "cIGZPersistResourceKeyList.h"
#include "GZServPtrs.h"
#include <d3d11.h>

LotCacheManager::LotCacheManager(cISC4City* city)
    : pCity(city), cacheInitialized(false) {
}

LotCacheManager::~LotCacheManager() {
    // Detach async build future to avoid blocking during shutdown
    if (buildFuture.valid()) {
        LOG_INFO("Detaching cache build future during LotCacheManager shutdown");
        static std::vector<std::future<void>> detachedFutures;
        detachedFutures.push_back(std::move(buildFuture));
    }

    Clear();
}

void LotCacheManager::Clear() {
    lotConfigCache.clear();
    exemplarCache.clear();
    cacheInitialized = false;
}

void LotCacheManager::BuildExemplarCache() {
    if (!exemplarCache.empty()) return;

    LOG_INFO("Building exemplar cache...");

    cIGZPersistResourceManagerPtr pRM;
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
    for (int i = 0; i < pKeyList->Size(); i++) {
        auto key = pKeyList->GetKey(i);

        // Only cache exemplars (0x6534284A), skip PNGs, LTEXTs, etc.
        if (key.type != kExemplarType) continue;

        cRZAutoRefCount<cISCPropertyHolder> exemplar;
        if (pRM->GetResource(key, GZIID_cISCPropertyHolder, exemplar.AsPPVoid(), 0, nullptr)) {
            // Store by instance ID -> vector of (group, exemplar) pairs
            exemplarCache[key.instance].emplace_back(key.group, exemplar);
            exemplarCount++;
        }
    }

    LOG_INFO("Exemplar cache built: {} exemplars across {} unique instance IDs", exemplarCount, exemplarCache.size());
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
        for (auto& [group, exemplar] : it->second) {
            uint32_t actualType;
            if (GetPropertyUint32(exemplar, exemplarTypePropertyID, actualType) && actualType == expectedTypeValue) {
                outExemplar = exemplar;
                return true;
            }
        }
    }
    return false;
}

void LotCacheManager::BuildLotConfigCache() {
    if (cacheInitialized || !pCity) return;

    LOG_INFO("Building lot configuration cache...");

    cISC4LotConfigurationManager* pLotConfigMgr = pCity->GetLotConfigurationManager();
    if (!pLotConfigMgr) return;

    cIGZPersistResourceManagerPtr pRM;
    if (!pRM) return;

    // Pre-allocate capacity to avoid rehashing during population
    lotConfigCache.reserve(2048);

    constexpr uint32_t kPropertyExemplarType = 0x00000010;
    constexpr uint32_t kPropertyExemplarTypeBuilding = 0x00000002;

    // Reuse hash set across iterations to avoid repeated allocations
    SC4HashSet<uint32_t> configIdTable{};
    configIdTable.Init(256);

    for (uint32_t x = 1; x <= 16; x++) {
        for (uint32_t z = 1; z <= 16; z++) {
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
                                cRZBaseString displayName;
                                if (PropertyUtil::GetDisplayName(pBuildingExemplar, displayName)) {
                                    cRZBaseString techName;
                                    pConfig->GetName(techName);
                                    // Optimize string concatenation with reserve
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

                                    // Load the icon PNG data
                                    std::vector<uint8_t> pngBytes;
                                    if (ExemplarUtil::LoadPNGByInstance(pRM, iconInstance, pngBytes) && !pngBytes.empty()) {
                                        // Get D3D11 device and create texture
                                        ID3D11Device* device = D3D11Hook::GetDevice();
                                        if (device) {
                                            ID3D11ShaderResourceView* srv = nullptr;
                                            int w = 0, h = 0;
                                            if (gfx::CreateSRVFromPNGMemory(pngBytes.data(), pngBytes.size(), device, &srv, &w, &h)) {
                                                entry.iconSRV = srv;
                                                entry.iconWidth = w;
                                                entry.iconHeight = h;
                                            }
                                        }
                                    }
                                }

                                // Occupant Groups
                                {
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
                    }

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

    cacheInitialized = true;
    LOG_INFO("Lot configuration cache built: {} entries", lotConfigCache.size());
}

void LotCacheManager::BuildLotConfigCacheAsync(std::function<void()> onComplete) {
    if (buildFuture.valid()) {
        LOG_WARN("Async cache build already in progress");
        return;
    }

    buildFuture = std::async(std::launch::async, [this, onComplete]() {
        BuildExemplarCache();
        BuildLotConfigCache();
        if (onComplete) {
            onComplete();
        }
    });
}

bool LotCacheManager::IsAsyncBuildInProgress() const {
    return buildFuture.valid() && buildFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready;
}

void LotCacheManager::CancelAsyncBuild() {
    if (buildFuture.valid()) {
        LOG_INFO("Detaching cache build future");
        static std::vector<std::future<void>> detachedFutures;
        detachedFutures.push_back(std::move(buildFuture));
    }
}
