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

class cISC4City;
class cIGZImGuiService;

/**
 * @brief Abstract base class for incremental cache builders
 *
 * Provides a common interface for cache orchestrators that support:
 * - Phase-based state machine (BuildingPhase → Complete)
 * - Incremental batch processing (spread across frames)
 * - UI feedback and cancellation
 *
 * Call SetImGuiService() once at initialization, then StartBuildCache() for each build.
 * Derived classes implement the phase-specific logic.
 */
class CacheBuildOrchestratorBase {
public:
    virtual ~CacheBuildOrchestratorBase() = default;

    /**
     * @brief Set the ImGui service (call once at initialization)
     * @param pImGuiService ImGui service instance
     */
    virtual void SetImGuiService(cIGZImGuiService* pImGuiService) = 0;

    /**
     * @brief Start the incremental cache build process
     * @param pCity The city instance
     * @return true if started successfully, false otherwise
     */
    virtual bool StartBuildCache(cISC4City* pCity) = 0;

    /**
     * @brief Update the cache build (call once per frame until complete)
     * @return true if still building, false if complete
     */
    virtual bool Update() = 0;

    /**
     * @brief Cancel the ongoing cache build
     */
    virtual void Cancel() = 0;

    /**
     * @brief Check if cache is currently being built
     */
    [[nodiscard]] virtual bool IsBuilding() const = 0;
};
