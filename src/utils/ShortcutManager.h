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
#include <vector>

class cISC4View3DWin;
class cIGZMessageServer2;
class cIGZMessageTarget2;

/**
 * @brief Manages keyboard shortcut registration with SimCity 4's input system
 *
 * This class encapsulates the complexity of loading KeyConfig resources from
 * DBPF files and registering shortcuts with the View3D window's key accelerator.
 * It also handles message notification registration for shortcut events.
 */
class ShortcutManager {
public:
    /**
     * @brief Construct a ShortcutManager with KeyConfig resource identifiers
     * @param keyConfigType DBPF type ID for KeyConfig resource
     * @param keyConfigGroup DBPF group ID for KeyConfig resource
     * @param keyConfigInstance DBPF instance ID for KeyConfig resource
     */
    ShortcutManager(
        uint32_t keyConfigType,
        uint32_t keyConfigGroup,
        uint32_t keyConfigInstance);

    /**
     * @brief Register shortcuts and their associated message notifications
     * @param pView3D The View3D window to register accelerators with
     * @param pMS2 The message server to register notifications with
     * @param messageTarget The object to receive shortcut messages (typically a director)
     * @param messageIDs List of message IDs to register for notifications
     * @return true if registration succeeded, false otherwise
     */
    bool RegisterShortcuts(
        cISC4View3DWin* pView3D,
        cIGZMessageServer2* pMS2,
        cIGZMessageTarget2* messageTarget,
        const std::vector<uint32_t>& messageIDs);

    /**
     * @brief Unregister message notifications for shortcuts
     * @param messageTarget The object to unregister (typically a director)
     * @param messageIDs List of message IDs to unregister
     */
    void UnregisterShortcuts(
        cIGZMessageTarget2* messageTarget,
        const std::vector<uint32_t>& messageIDs);

private:
    uint32_t keyConfigType;
    uint32_t keyConfigGroup;
    uint32_t keyConfigInstance;
};