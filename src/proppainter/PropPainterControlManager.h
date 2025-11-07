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

#include <cstdint>
#include "cRZAutoRefCount.h"
#include "PropPainterInputControl.h"

class PropCacheManager;
class PropPainterUI;
class cISC4City;
class cISC4View3DWin;

/**
 * @brief Manages the PropPainterInputControl lifecycle and coordination
 *
 * This class encapsulates the creation, initialization, and activation of
 * the prop painter input control, coordinating between the View3D window,
 * the prop cache, and the prop painter UI.
 */
class PropPainterControlManager {
public:
    /**
     * @brief Construct manager with references to dependencies
     * @param cacheManager The prop cache to look up prop details
     * @param ui The prop painter UI for preview rendering
     */
    PropPainterControlManager(PropCacheManager& cacheManager, PropPainterUI& ui);

    /**
     * @brief Start prop painting mode
     * @param propID The prop to paint
     * @param rotation The rotation angle for the prop
     * @param pCity The city instance
     * @param pView3D The View3D window
     * @return true if started successfully, false otherwise
     */
    bool StartPainting(
        uint32_t propID,
        int rotation,
        cISC4City* pCity,
        cISC4View3DWin* pView3D);

    /**
     * @brief Stop prop painting mode
     * @param pView3D The View3D window
     */
    void StopPainting(cISC4View3DWin* pView3D);

    /**
     * @brief Check if currently painting
     */
    bool IsPainting() const;

private:
    PropCacheManager& cacheManager;
    PropPainterUI& ui;
    cRZAutoRefCount<PropPainterInputControl> pControl;
    bool isPainting;
};