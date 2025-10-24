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
#include <vector>
#include <unordered_map>
#include <cstdint>

class cISC4LotConfiguration;
class cISC4City;
struct LotConfigEntry;

/**
 * Filters lot configurations based on zone, wealth, size, search text, and occupant groups.
 */
class LotFilterer {
public:
    /**
     * Filter lots from cache and populate the output list.
     */
    static void FilterLots(
        cISC4City* pCity,
        const std::unordered_map<uint32_t, LotConfigEntry>& lotConfigCache,
        std::vector<LotConfigEntry>& outFilteredEntries,
        uint8_t filterZoneType,
        uint8_t filterWealthType,
        uint32_t minSizeX, uint32_t maxSizeX,
        uint32_t minSizeZ, uint32_t maxSizeZ,
        const char* searchBuffer,
        const std::vector<uint32_t>& selectedOccupantGroups
    );

private:
    // Check if lot config matches zone filter
    static bool MatchesZoneFilter(cISC4LotConfiguration* pConfig, uint8_t filterZoneType);

    // Check if lot config matches wealth filter
    static bool MatchesWealthFilter(cISC4LotConfiguration* pConfig, uint8_t filterWealthType);

    // Check if lot entry matches search text
    static bool MatchesSearchFilter(const LotConfigEntry& entry, const char* searchText);

    // Check if lot entry matches occupant group filter
    static bool MatchesOccupantGroupFilter(const LotConfigEntry& entry, const std::vector<uint32_t>& selectedGroups);
};
