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

#include <deque>
#include <unordered_map>
#include <cstdint>
#include "../ui/LotConfigEntry.h"

/**
 * @brief Manages lazy icon loading for lot configurations
 *
 * Responsibilities:
 * - Queueing icon load requests
 * - Processing icon decode jobs incrementally
 * - Managing icon job queue
 */
class IconLoadingService {
public:
    IconLoadingService() = default;
    ~IconLoadingService() = default;

    // Icon request management
    void RequestIcon(uint32_t lotID, std::unordered_map<uint32_t, LotConfigEntry>& lotConfigCache);
    void ProcessIconJobs(
        uint32_t maxJobsPerFrame,
        std::unordered_map<uint32_t, LotConfigEntry>& lotConfigCache,
        std::unordered_map<uint32_t, size_t>& lotEntryIndexByID,
        std::vector<LotConfigEntry>& lotEntries
    );

    // Queue management
    void Clear();
    size_t GetQueueSize() const { return iconJobQueue.size(); }

private:
    std::deque<uint32_t> iconJobQueue;
};
