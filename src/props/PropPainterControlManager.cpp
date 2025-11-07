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
#include "PropPainterControlManager.h"

#include "cISC4City.h"
#include "cISC4View3DWin.h"
#include "PropPainterUI.h"
#include "../cache/PropCacheManager.h"
#include "../utils/Logger.h"

PropPainterControlManager::PropPainterControlManager(
    PropCacheManager& cacheManager,
    PropPainterUI& ui)
    : cacheManager(cacheManager)
    , ui(ui)
    , isPainting(false) {
}

bool PropPainterControlManager::StartPainting(
    uint32_t propID,
    int rotation,
    cISC4City* pCity,
    cISC4View3DWin* pView3D) {

    if (!pView3D || !pCity) {
        LOG_ERROR("Cannot start prop painting: view or city not available");
        return false;
    }

    // Get prop name from cache
    std::string propName = "Unknown Prop";
    const PropCacheEntry* entry = cacheManager.GetPropByID(propID);
    if (entry) {
        propName = entry->name;
    }

    // Create input control if it doesn't exist
    if (!pControl) {
        pControl = new PropPainterInputControl();
        pControl->SetCity(pCity);
        pControl->SetWindow(pView3D->AsIGZWin());
        pControl->Init();

        // Set up preview rendering
        ui.SetInputControl(pControl);
        ui.SetRenderer(pView3D->GetRenderer());

        LOG_DEBUG("Created and initialized PropPainterInputControl");
    }

    // Set the prop to paint (with name for preview)
    pControl->SetPropToPaint(propID, rotation, propName);

    // Remove all existing input controls and activate the prop painter
    pView3D->RemoveAllViewInputControls(false);
    pView3D->SetCurrentViewInputControl(
        pControl,
        cISC4View3DWin::ViewInputControlStackOperation_None
    );

    isPainting = true;
    LOG_INFO("Started prop painting mode for prop {} (0x{:08X}), rotation {}", propName, propID, rotation);
    return true;
}

void PropPainterControlManager::StopPainting(cISC4View3DWin* pView3D) {
    if (!pView3D) {
        LOG_WARN("Cannot stop prop painting: View3D is null");
        return;
    }

    if (!isPainting) {
        return;
    }

    // Remove the prop painter input control
    pView3D->RemoveCurrentViewInputControl(false);

    isPainting = false;
    LOG_INFO("Stopped prop painting mode");
}

bool PropPainterControlManager::IsPainting() const {
    return isPainting;
}