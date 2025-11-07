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
#include <d3d11.h>
#include <cache/LotCacheManager.h>

#include "cGZPersistResourceKey.h"
#include "cIGZPersistResourceKeyList.h"
#include "cIGZPersistResourceManager.h"
#include "cIGZVariant.h"
#include "cISC4City.h"
#include "cISC4LotConfiguration.h"
#include "cISC4LotConfigurationManager.h"
#include "cISCProperty.h"
#include "cRZBaseString.h"
#include "GZServPtrs.h"
#include "SC4HashSet.h"
#include "../exemplar/ExemplarUtil.h"
#include "../exemplar/IconResourceUtil.h"
#include "../exemplar/PropertyUtil.h"
#include "../gfx/IconLoader.h"
#include "../gfx/DX11ImageLoader.h"
#include "../gfx/TextureToPNG.h"
#include "../s3d/S3DThumbnailGenerator.h"
#include "../utils/Logger.h"
#include "CacheDatabase.h"

LotCacheManager::LotCacheManager()
    : cacheInitialized(false),
      currentLotSizeIndex(0),
      processedLotCount(0),
      totalLotCount(0),
      pCityForIncremental(nullptr) {
}

LotCacheManager::~LotCacheManager() {
    Clear();
}

void LotCacheManager::Clear() {
    // Release all icon SRVs (PNG or S3D)
    for (auto& kv : lotConfigCache) {
        auto& entry = kv.second;
        if (entry.iconSRV) {
            entry.iconSRV->Release();
            entry.iconSRV = nullptr;
        }
        entry.iconType = LotConfigEntry::IconType::None;
    }

    lotConfigCache.clear();
    exemplarCache.clear();
    cacheInitialized = false;
}

void LotCacheManager::BuildCache(cISC4City* pCity, cIGZPersistResourceManager* pRM, ID3D11Device* pDevice, LotCacheProgressCallback progressCallback) {
    if (cacheInitialized) return;

    LOG_INFO("Building lot cache...");
    BuildExemplarCache(pRM, progressCallback);
    BuildLotConfigCache(pCity, pRM, pDevice, progressCallback);
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

void LotCacheManager::BuildLotConfigCache(cISC4City* pCity, cIGZPersistResourceManager* pRM, ID3D11Device* pDevice, LotCacheProgressCallback progressCallback) {
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
                    uint32_t lotConfigID = it;

                    if (lotConfigCache.contains(lotConfigID)) continue;

                    cISC4LotConfiguration* pConfig = pLotConfigMgr->GetLotConfiguration(lotConfigID);
                    if (!pConfig) continue;

                    LotConfigEntry entry;
                    entry.id = lotConfigID;

                    cRZAutoRefCount<cISCPropertyHolder> pLotExemplar;
                    if (GetCachedExemplar(lotConfigID, pLotExemplar)) {
                        // Get exemplar group from the cached exemplar
                        auto it = exemplarCache.find(lotConfigID);
                        if (it != exemplarCache.end() && !it->second.empty()) {
                            entry.exemplarGroup = it->second[0].first; // first element is (groupID, exemplar)
                        }
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

                                    if (pDevice) {
                                        ID3D11ShaderResourceView* srv = nullptr;
                                        int w = 0, h = 0;
                                        if (IconLoader::LoadIconFromPNG(pRM, iconInstance, pDevice, &srv, &w, &h)) {
                                            entry.iconSRV = srv;
                                            entry.iconWidth = w;
                                            entry.iconHeight = h;
                                            entry.iconType = LotConfigEntry::IconType::PNG;
                                        }
                                    }
                                }

                                // If no PNG icon loaded, try S3D thumbnail as fallback
                                if (entry.iconType == LotConfigEntry::IconType::None && pDevice) {
                                    // Get device context for rendering
                                    ID3D11DeviceContext* pContext = nullptr;
                                    pDevice->GetImmediateContext(&pContext);

                                    if (pContext) {
                                        // Generate S3D thumbnail (64x64, zoom 5, rotation 0 for standard view)
                                        ID3D11ShaderResourceView* s3dSRV = S3D::ThumbnailGenerator::GenerateThumbnailFromExemplar(
                                            pBuildingExemplar, pRM, pDevice, pContext, 44, 5, 0
                                        );

                                        if (s3dSRV) {
                                            entry.iconSRV = s3dSRV;
                                            entry.iconWidth = 44;
                                            entry.iconHeight = 44;
                                            entry.iconType = LotConfigEntry::IconType::S3D;
                                            LOG_DEBUG("Generated S3D thumbnail for lot 0x{:08X} ({})", lotConfigID, entry.name);
                                        }

                                        pContext->Release();
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

// Incremental cache building methods

void LotCacheManager::BeginIncrementalBuild() {
    exemplarCache.clear();
    lotConfigCache.clear();
    lotSizesToProcess.clear();
    currentLotSizeIndex = 0;
    processedLotCount = 0;
    totalLotCount = 0;
    pCityForIncremental = nullptr;
    cacheInitialized = false;
}

void LotCacheManager::BuildExemplarCacheSync(cIGZPersistResourceManager* pRM) {
    if (!exemplarCache.empty()) return;

    LOG_INFO("Building exemplar cache (sync)...");

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

void LotCacheManager::BeginLotConfigProcessing(cISC4City* pCity) {
    pCityForIncremental = pCity;

    // Build list of lot sizes to process
    lotSizesToProcess.clear();
    for (uint32_t x = 1; x <= 16; x++) {
        for (uint32_t z = 1; z <= 16; z++) {
            lotSizesToProcess.push_back({x, z});
        }
    }

    currentLotSizeIndex = 0;
    processedLotCount = 0;
    totalLotCount = static_cast<int>(lotSizesToProcess.size());

    lotConfigCache.reserve(2048);
}

int LotCacheManager::ProcessLotConfigBatch(cIGZPersistResourceManager* pRM, ID3D11Device* pDevice, int maxLotsToProcess) {
    if (!pCityForIncremental || !pRM) return 0;

    cISC4LotConfigurationManager* pLotConfigMgr = pCityForIncremental->GetLotConfigurationManager();
    if (!pLotConfigMgr) return 0;

    constexpr uint32_t kPropertyExemplarType = 0x00000010;
    constexpr uint32_t kPropertyExemplarTypeBuilding = 0x00000002;

    SC4HashSet<uint32_t> configIdTable{};

    int processedThisBatch = 0;

    // Process lots by size, but limit to maxLotsToProcess individual lots per batch
    while (currentLotSizeIndex < lotSizesToProcess.size() && processedThisBatch < maxLotsToProcess) {
        const auto& [x, z] = lotSizesToProcess[currentLotSizeIndex];

        bool finishedThisSize = true; // Track if we finished processing all new lots in this size

        if (pLotConfigMgr->GetLotConfigurationIDsBySize(configIdTable, x, z)) {
            for (const auto it : configIdTable) {
                // Check if we've hit the batch limit before processing this lot
                if (processedThisBatch >= maxLotsToProcess) {
                    finishedThisSize = false;
                    break;
                }

                uint32_t lotConfigID = it;

                if (lotConfigCache.count(lotConfigID)) continue;

                cISC4LotConfiguration* pConfig = pLotConfigMgr->GetLotConfiguration(lotConfigID);
                if (!pConfig) continue;

                LotConfigEntry entry;
                entry.id = lotConfigID;

                cRZAutoRefCount<cISCPropertyHolder> pLotExemplar;
                if (GetCachedExemplar(lotConfigID, pLotExemplar)) {
                    // Get exemplar group from the cached exemplar
                    auto it = exemplarCache.find(lotConfigID);
                    if (it != exemplarCache.end() && !it->second.empty()) {
                        entry.exemplarGroup = it->second[0].first; // first element is (groupID, exemplar)
                    }
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

                                if (pDevice) {
                                    ID3D11ShaderResourceView* srv = nullptr;
                                    int w = 0, h = 0;
                                    if (IconLoader::LoadIconFromPNG(pRM, iconInstance, pDevice, &srv, &w, &h)) {
                                        entry.iconSRV = srv;
                                        entry.iconWidth = w;
                                        entry.iconHeight = h;
                                        entry.iconType = LotConfigEntry::IconType::PNG;
                                    }
                                }
                            }

                            // If no PNG icon loaded, try S3D thumbnail as fallback
                            if (entry.iconType == LotConfigEntry::IconType::None && pDevice) {
                                ID3D11DeviceContext* pContext = nullptr;
                                pDevice->GetImmediateContext(&pContext);


                                if (pContext) {
                                    ID3D11ShaderResourceView* s3dSRV = S3D::ThumbnailGenerator::GenerateThumbnailFromExemplar(
                                        pBuildingExemplar, pRM, pDevice, pContext, kThumbnailSize, 5, 0
                                    );

                                    if (s3dSRV) {
                                        entry.iconSRV = s3dSRV;
                                        entry.iconWidth = kThumbnailSize;
                                        entry.iconHeight = kThumbnailSize;
                                        entry.iconType = LotConfigEntry::IconType::S3D;
                                        LOG_DEBUG("Generated S3D thumbnail for lot 0x{:08X} ({})", lotConfigID, entry.name);
                                    }

                                    pContext->Release();
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

                // Increment per individual lot, not per lot size
                processedThisBatch++;
            }
        }

        // Only advance to next size if we finished processing all new lots in current size
        if (finishedThisSize) {
            currentLotSizeIndex++;
            processedLotCount++;
        }
    }

    return processedThisBatch;
}

void LotCacheManager::FinalizeIncrementalBuild() {
    cacheInitialized = true;
    pCityForIncremental = nullptr;
    LOG_INFO("Incremental cache build finalized: {} lot entries", lotConfigCache.size());
}

bool LotCacheManager::LoadFromDatabase(const std::filesystem::path& dbPath, ID3D11Device* pDevice, ID3D11DeviceContext* pContext) {
    if (!pDevice || !pContext) {
        Logger::LOG_ERROR("Invalid device or context for cache loading");
        return false;
    }

    CacheDatabase db;
    if (!db.OpenOrCreate(dbPath)) {
        Logger::LOG_ERROR("Failed to open cache database: {}", dbPath.string());
        return false;
    }

    // Validate cache version
    std::string version = db.GetMetadata("cache_version");
    if (version != "1") {
        Logger::LOG_WARN("Cache version mismatch (expected 1, got {}), rebuild required", version);
        return false;
    }

    // Load all lot keys
    auto keys = db.GetAllLotKeys();
    if (keys.empty()) {
        Logger::LOG_WARN("Cache database is empty");
        return false;
    }

    Logger::LOG_INFO("Loading {} lots from cache database...", keys.size());

    int loadedCount = 0;
    for (auto [group, instance] : keys) {
        auto result = db.LoadLot(group, instance);
        if (!result) {
            Logger::LOG_WARN("Failed to load lot {}/{} from database", group, instance);
            continue;
        }

        auto& [lot, pngBlob] = *result;

        // Create texture from PNG BLOB
        if (!pngBlob.empty()) {
            ID3D11ShaderResourceView* srv = nullptr;
            int w = 0, h = 0;
            if (gfx::CreateSRVFromPNGMemory(pngBlob.data(), pngBlob.size(), pDevice, &srv, &w, &h)) {
                lot.iconSRV = srv;
                lot.iconWidth = w;
                lot.iconHeight = h;
            } else {
                Logger::LOG_WARN("Failed to decode PNG thumbnail for lot {}", lot.id);
            }
        }

        // Store in cache (lot.id IS the instance ID)
        lotConfigCache[lot.id] = std::move(lot);
        loadedCount++;
    }

    cacheInitialized = (loadedCount > 0);
    Logger::LOG_INFO("Loaded {} lots from cache database in {}", loadedCount, dbPath.string());
    return cacheInitialized;
}

bool LotCacheManager::SaveToDatabase(const std::filesystem::path& dbPath, ID3D11Device* pDevice, ID3D11DeviceContext* pContext) {
    if (!pDevice || !pContext) {
        Logger::LOG_ERROR("Invalid device or context for cache saving");
        return false;
    }

    if (!cacheInitialized || lotConfigCache.empty()) {
        Logger::LOG_WARN("Cache not initialized or empty, nothing to save");
        return false;
    }

    CacheDatabase db;
    if (!db.OpenOrCreate(dbPath)) {
        Logger::LOG_ERROR("Failed to open cache database for saving: {}", dbPath.string());
        return false;
    }

    Logger::LOG_INFO("Saving {} lots to cache database...", lotConfigCache.size());

    // Use transaction for bulk insert (much faster)
    if (!db.BeginTransaction()) {
        Logger::LOG_ERROR("Failed to begin transaction");
        return false;
    }

    int savedCount = 0;
    int thumbnailCount = 0;
    for (const auto& [id, lot] : lotConfigCache) {
        // Encode thumbnail to PNG
        std::vector<uint8_t> pngBlob;
        if (lot.iconSRV) {
            ID3D11Resource* resource = nullptr;
            lot.iconSRV->GetResource(&resource);
            if (resource) {
                ID3D11Texture2D* texture = nullptr;
                HRESULT hr = resource->QueryInterface(__uuidof(ID3D11Texture2D),
                                                       reinterpret_cast<void**>(&texture));
                if (SUCCEEDED(hr) && texture) {
                    if (TextureToPNG::Encode(pDevice, pContext, texture, pngBlob)) {
                        thumbnailCount++;
                    } else {
                        Logger::LOG_WARN("Failed to encode PNG thumbnail for lot 0x{:08X}", id);
                    }
                    texture->Release();
                }
                resource->Release();
            }
        }

        // Save to database
        if (db.SaveLot(lot, pngBlob)) {
            savedCount++;
        } else {
            Logger::LOG_ERROR("Failed to save lot 0x{:08X} to database", id);
        }
    }

    // Set metadata
    db.SetMetadata("cache_version", "1");

    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
    localtime_s(&tm_now, &time_t_now);
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &tm_now);
    db.SetMetadata("last_build", timestamp);
    db.SetMetadata("lot_count", std::to_string(savedCount));

    if (!db.CommitTransaction()) {
        Logger::LOG_ERROR("Failed to commit transaction");
        return false;
    }

    Logger::LOG_INFO("Saved {} lots ({} with thumbnails) to cache database: {}", savedCount, thumbnailCount, dbPath.string());
    return true;
}
