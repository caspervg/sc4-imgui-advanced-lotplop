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
#include "../ui/LotConfigEntry.h"

class cISC4City;
class cISC4LotConfiguration;
class AdvancedLotPlopUI;

/**
 * @brief Manages filtering logic for lot configurations
 *
 * Responsibilities:
 * - Applying zone, wealth, size, and search filters
 * - Refreshing filtered lot list based on current criteria
 * - Managing filter state
 */
class LotFilterService {
public:
    explicit LotFilterService(cISC4City* city, AdvancedLotPlopUI* ui);
    ~LotFilterService() = default;

    // Filtering operations
    void RefreshLotList(
        const std::unordered_map<uint32_t, LotConfigEntry>& lotConfigCache,
        std::vector<LotConfigEntry>& lotEntries,
        std::unordered_map<uint32_t, size_t>& lotEntryIndexByID
    );

    bool MatchesFilters(cISC4LotConfiguration* pConfig) const;

private:
    cISC4City* pCity;
    AdvancedLotPlopUI* pUI;
};
