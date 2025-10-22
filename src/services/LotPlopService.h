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

class cISC4View3DWin;

/**
 * @brief Manages lot placement interaction with the game
 *
 * Responsibilities:
 * - Triggering the game's lot plop tool
 * - Creating and managing command parameters
 * - Game API integration for lot placement
 */
class LotPlopService {
public:
    explicit LotPlopService(cISC4View3DWin* view3D);
    ~LotPlopService() = default;

    // Lot plopping
    void TriggerLotPlop(uint32_t lotID);

    // State management
    void SetView3D(cISC4View3DWin* view3D) { pView3D = view3D; }

private:
    cISC4View3DWin* pView3D;
};
