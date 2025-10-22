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
#include "LotFilterService.h"
#include "../ui/AdvancedLotPlopUI.h"
#include "../utils/Logger.h"
#include "cISC4City.h"
#include "cISC4LotConfiguration.h"
#include "cISC4LotConfigurationManager.h"
#include "cISC4ZoneManager.h"
#include "cISC4BuildingOccupant.h"
#include <algorithm>
#include <cctype>

LotFilterService::LotFilterService(cISC4City* city, AdvancedLotPlopUI* ui)
    : pCity(city), pUI(ui) {
}

void LotFilterService::RefreshLotList(
    const std::unordered_map<uint32_t, LotConfigEntry>& lotConfigCache,
    std::vector<LotConfigEntry>& lotEntries,
    std::unordered_map<uint32_t, size_t>& lotEntryIndexByID
) {
    if (!pCity || !pUI) return;

    lotEntries.clear();
    lotEntryIndexByID.clear();

    cISC4LotConfigurationManager* pLotConfigMgr = pCity->GetLotConfigurationManager();
    if (!pLotConfigMgr) return;

    // Filter from cache based on current criteria
    for (uint32_t x = pUI->GetMinSizeX(); x <= pUI->GetMaxSizeX(); x++) {
        for (uint32_t z = pUI->GetMinSizeZ(); z <= pUI->GetMaxSizeZ(); z++) {
            SC4HashSet<uint32_t> configIdTable{};
            configIdTable.Init(8);

            if (pLotConfigMgr->GetLotConfigurationIDsBySize(configIdTable, x, z)) {
                for (const auto it : configIdTable) {
                    auto cacheIt = lotConfigCache.find(it->key);
                    if (cacheIt == lotConfigCache.end()) continue;

                    const LotConfigEntry& cachedEntry = cacheIt->second;

                    // Apply filters
                    cISC4LotConfiguration* pConfig = pLotConfigMgr->GetLotConfiguration(it->key);
                    if (!pConfig || !MatchesFilters(pConfig)) continue;

                    // Search filter
                    if (pUI->GetSearchBuffer()[0] != '\0') {
                        std::string searchLower = pUI->GetSearchBuffer();
                        std::transform(searchLower.begin(), searchLower.end(),
                                       searchLower.begin(),
                                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                        std::string nameLower = cachedEntry.name;
                        std::transform(nameLower.begin(), nameLower.end(),
                                       nameLower.begin(),
                                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                        // Match against name OR description
                        if (nameLower.find(searchLower) == std::string::npos) {
                            std::string descLower = cachedEntry.description;
                            std::transform(descLower.begin(), descLower.end(),
                                           descLower.begin(),
                                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                            if (descLower.find(searchLower) == std::string::npos) continue;
                        }
                    }

                    // Occupant group filter (Any-match)
                    const auto& selGroups = pUI->GetSelectedOccupantGroups();
                    if (!selGroups.empty()) {
                        bool any = false;
                        for (uint32_t g : selGroups) {
                            if (cachedEntry.occupantGroups.count(g) != 0) {
                                any = true;
                                break;
                            }
                        }
                        if (!any) continue;
                    }

                    lotEntries.push_back(cachedEntry);
                    lotEntryIndexByID[cachedEntry.id] = lotEntries.size() - 1;
                }
            }
        }
    }
}

bool LotFilterService::MatchesFilters(cISC4LotConfiguration* pConfig) const {
    if (!pConfig || !pUI) return false;

    // Zone filter: UI exposes R/C/I categories, not exact zone densities.
    // Map UI value (0=R,1=C,2=I) to the set of densities and check compatibility with any of them.
    if (pUI->GetFilterZoneType() != 0xFF) {
        const uint8_t zoneCategory = pUI->GetFilterZoneType();
        bool zoneOk = false;
        if (zoneCategory == 0) {
            // Residential: low, medium, high
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
            // Commercial: low, medium, high
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
            // Industrial: medium, high (Agriculture is separate and not included in generic I)
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
            // Other: Military, Airport, Seaport, Spaceport, Landfill
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
        if (!zoneOk) return false;
    }

    // Wealth filter: UI stores 0=Low,1=Medium,2=High; enum is 1..3
    if (pUI->GetFilterWealthType() != 0xFF) {
        const uint8_t wealthIdx = pUI->GetFilterWealthType();
        cISC4BuildingOccupant::WealthType wealthType =
                static_cast<cISC4BuildingOccupant::WealthType>(wealthIdx + 1);
        if (!pConfig->IsCompatibleWithWealthType(wealthType)) return false;
    }

    return true;
}
