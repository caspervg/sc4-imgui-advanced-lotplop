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
class PropCacheManager;
class PropPainterUI;
struct ID3D11Device;

/**
 * @brief Orchestrates prop cache building with UI feedback and error handling
 *
 * This class separates the cache building orchestration logic from the director,
 * handling UI updates, resource acquisition, and preparing for future disk-based
 * caching (SQLite integration).
 */
class PropCacheBuildOrchestrator {
public:
    /**
     * @brief Construct orchestrator with references to cache manager and UI
     * @param cacheManager The prop cache manager to build
     * @param ui The UI to show progress updates
     */
    PropCacheBuildOrchestrator(PropCacheManager& cacheManager, PropPainterUI& ui);

    /**
     * @brief Build the prop cache (or load from disk in future)
     * @param pCity The city instance
     * @param pDevice D3D11 device for thumbnail generation
     * @return true if successful, false otherwise
     */
    bool BuildCache(cISC4City* pCity, ID3D11Device* pDevice);

    /**
     * @brief Check if cache is currently being built
     */
    bool IsBuilding() const { return isBuilding; }

private:
    PropCacheManager& cacheManager;
    PropPainterUI& ui;
    bool isBuilding;
};