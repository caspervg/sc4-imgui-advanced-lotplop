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
#include "LotFilterer.h"

#include <algorithm>
#include <cctype>
#include <string>

#include "cISC4BuildingOccupant.h"
#include "cISC4City.h"
#include "cISC4LotConfiguration.h"
#include "cISC4LotConfigurationManager.h"
#include "cISC4ZoneManager.h"
#include "SC4HashSet.h"
#include "ui/LotConfigEntry.h"

void LotFilterer::FilterLots(
    cISC4City* pCity,
    const std::unordered_map<uint32_t, LotConfigEntry>& lotConfigCache,
    std::vector<LotConfigEntry>& outFilteredEntries,
    uint8_t filterZoneType,
    uint8_t filterWealthType,
    uint32_t minSizeX, uint32_t maxSizeX,
    uint32_t minSizeZ, uint32_t maxSizeZ,
    const char* searchBuffer,
    const std::vector<uint32_t>& selectedOccupantGroups
) {
    outFilteredEntries.clear();

    cISC4LotConfigurationManager* pLotConfigMgr = pCity->GetLotConfigurationManager();
    if (!pLotConfigMgr) return;

    SC4HashSet<uint32_t> configIdTable{};
    configIdTable.Init(8);

    for (uint32_t x = minSizeX; x <= maxSizeX; x++) {
        for (uint32_t z = minSizeZ; z <= maxSizeZ; z++) {
            // Reset the temporary set per size to avoid accumulating IDs across iterations
            configIdTable.Clear();
            configIdTable.Init(8);
            if (pLotConfigMgr->GetLotConfigurationIDsBySize(configIdTable, x, z)) {
                for (const auto it : configIdTable) {
                    auto cacheIt = lotConfigCache.find(it->key);
                    if (cacheIt == lotConfigCache.end()) continue;

                    const LotConfigEntry& cachedEntry = cacheIt->second;

                    // Get lot config for filtering
                    cISC4LotConfiguration* pConfig = pLotConfigMgr->GetLotConfiguration(it->key);
                    if (!pConfig) continue;

                    // Apply filters
                    if (!MatchesZoneFilter(pConfig, filterZoneType)) continue;
                    if (!MatchesWealthFilter(pConfig, filterWealthType)) continue;
                    if (!MatchesSearchFilter(cachedEntry, searchBuffer)) continue;
                    if (!MatchesOccupantGroupFilter(cachedEntry, selectedOccupantGroups)) continue;

                    outFilteredEntries.push_back(cachedEntry);
                }
            }
        }
    }
}

bool LotFilterer::MatchesZoneFilter(cISC4LotConfiguration* pConfig, uint8_t filterZoneType) {
    if (filterZoneType == 0xFF) return true; // Any

    const uint8_t zoneCategory = filterZoneType;
    bool zoneOk = false;

    if (zoneCategory == 0) {
        // Residential
        cISC4ZoneManager::ZoneType resZones[] = {
            cISC4ZoneManager::ZoneType::ResidentialLowDensity,
            cISC4ZoneManager::ZoneType::ResidentialMediumDensity,
            cISC4ZoneManager::ZoneType::ResidentialHighDensity,
        };
        for (auto z : resZones) {
            if (pConfig->IsCompatibleWithZoneType(z)) {
                zoneOk = true;
                break;
            }
        }
    } else if (zoneCategory == 1) {
        // Commercial
        cISC4ZoneManager::ZoneType comZones[] = {
            cISC4ZoneManager::ZoneType::CommercialLowDensity,
            cISC4ZoneManager::ZoneType::CommercialMediumDensity,
            cISC4ZoneManager::ZoneType::CommercialHighDensity,
        };
        for (auto z : comZones) {
            if (pConfig->IsCompatibleWithZoneType(z)) {
                zoneOk = true;
                break;
            }
        }
    } else if (zoneCategory == 2) {
        // Industrial
        cISC4ZoneManager::ZoneType indZones[] = {
            cISC4ZoneManager::ZoneType::IndustrialMediumDensity,
            cISC4ZoneManager::ZoneType::IndustrialHighDensity,
        };
        for (auto z : indZones) {
            if (pConfig->IsCompatibleWithZoneType(z)) {
                zoneOk = true;
                break;
            }
        }
    } else if (zoneCategory == 3) {
        // Agriculture
        if (pConfig->IsCompatibleWithZoneType(cISC4ZoneManager::ZoneType::Agriculture)) zoneOk = true;
    } else if (zoneCategory == 4) {
        // Plopped
        if (pConfig->IsCompatibleWithZoneType(cISC4ZoneManager::ZoneType::Plopped)) zoneOk = true;
    } else if (zoneCategory == 5) {
        // None
        if (pConfig->IsCompatibleWithZoneType(cISC4ZoneManager::ZoneType::None)) zoneOk = true;
    } else if (zoneCategory == 6) {
        // Other
        cISC4ZoneManager::ZoneType otherZones[] = {
            cISC4ZoneManager::ZoneType::Military,
            cISC4ZoneManager::ZoneType::Airport,
            cISC4ZoneManager::ZoneType::Seaport,
            cISC4ZoneManager::ZoneType::Spaceport,
            cISC4ZoneManager::ZoneType::Landfill,
        };
        for (auto z : otherZones) {
            if (pConfig->IsCompatibleWithZoneType(z)) {
                zoneOk = true;
                break;
            }
        }
    }

    return zoneOk;
}

bool LotFilterer::MatchesWealthFilter(cISC4LotConfiguration* pConfig, uint8_t filterWealthType) {
    if (filterWealthType == 0xFF) return true; // Any

    const uint8_t wealthIdx = filterWealthType;
    cISC4BuildingOccupant::WealthType wealthType = static_cast<cISC4BuildingOccupant::WealthType>(wealthIdx + 1);
    return pConfig->IsCompatibleWithWealthType(wealthType);
}

bool LotFilterer::MatchesSearchFilter(const LotConfigEntry& entry, const char* searchText) {
    if (!searchText || searchText[0] == '\0') return true;

    std::string searchLower = searchText;
    std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    std::string nameLower = entry.name;
    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (nameLower.find(searchLower) != std::string::npos) return true;

    // Also search description if available
    std::string descLower = entry.description;
    std::transform(descLower.begin(), descLower.end(), descLower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    return descLower.find(searchLower) != std::string::npos;
}

bool LotFilterer::MatchesOccupantGroupFilter(const LotConfigEntry& entry, const std::vector<uint32_t>& selectedGroups) {
    if (selectedGroups.empty()) return true;

    for (uint32_t g : selectedGroups) {
        if (entry.occupantGroups.count(g) != 0) {
            return true; // Any-match
        }
    }
    return false;
}
