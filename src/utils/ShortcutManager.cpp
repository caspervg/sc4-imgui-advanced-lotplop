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
#include "ShortcutManager.h"
#include "Logger.h"
#include "cGZPersistResourceKey.h"
#include "cIGZPersistResourceManager.h"
#include "cIGZWinKeyAcceleratorRes.h"
#include "cIGZMessageServer2.h"
#include "cISC4View3DWin.h"
#include "cRZAutoRefCount.h"
#include "GZServPtrs.h"

ShortcutManager::ShortcutManager(
    uint32_t keyConfigType,
    uint32_t keyConfigGroup,
    uint32_t keyConfigInstance)
    : keyConfigType(keyConfigType)
    , keyConfigGroup(keyConfigGroup)
    , keyConfigInstance(keyConfigInstance) {
}

bool ShortcutManager::RegisterShortcuts(
    cISC4View3DWin* pView3D,
    cIGZMessageServer2* pMS2,
    cIGZMessageTarget2* messageTarget,
    const std::vector<uint32_t>& messageIDs) {

    if (!pView3D) {
        LOG_ERROR("Cannot register shortcuts: View3D is null");
        return false;
    }

    if (!pMS2) {
        LOG_ERROR("Cannot register shortcuts: Message server is null");
        return false;
    }

    if (!messageTarget) {
        LOG_ERROR("Cannot register shortcuts: Message target is null");
        return false;
    }

    // Get resource manager
    cIGZPersistResourceManagerPtr pRM;
    if (!pRM) {
        LOG_ERROR("Cannot register shortcuts: Resource manager unavailable");
        return false;
    }

    // Load KeyConfig resource
    cRZAutoRefCount<cIGZWinKeyAcceleratorRes> pAcceleratorRes;
    const cGZPersistResourceKey key(keyConfigType, keyConfigGroup, keyConfigInstance);
    if (!pRM->GetPrivateResource(key, kGZIID_cIGZWinKeyAcceleratorRes, pAcceleratorRes.AsPPVoid(), 0, nullptr)) {
        LOG_ERROR("Cannot register shortcuts: Failed to load KeyConfig resource (Type: 0x{:08X}, Group: 0x{:08X}, Instance: 0x{:08X})",
            keyConfigType, keyConfigGroup, keyConfigInstance);
        return false;
    }

    // Get key accelerator from View3D
    auto* pAccel = pView3D->GetKeyAccelerator();
    if (!pAccel) {
        LOG_ERROR("Cannot register shortcuts: Failed to get key accelerator from View3D");
        return false;
    }

    // Register accelerator resources
    if (!pAcceleratorRes->RegisterResources(pAccel)) {
        LOG_ERROR("Cannot register shortcuts: Failed to register accelerator resources");
        return false;
    }

    // Register message notifications
    for (uint32_t messageID : messageIDs) {
        if (!pMS2->AddNotification(messageTarget, messageID)) {
            LOG_WARN("Failed to register notification for message ID 0x{:08X}", messageID);
        } else {
            LOG_DEBUG("Registered notification for message ID 0x{:08X}", messageID);
        }
    }

    LOG_INFO("Shortcuts registered successfully");
    return true;
}

void ShortcutManager::UnregisterShortcuts(
    cIGZMessageTarget2* messageTarget,
    const std::vector<uint32_t>& messageIDs) {

    if (!messageTarget) {
        LOG_ERROR("Cannot unregister shortcuts: Message target is null");
        return;
    }

    // Get message server
    cIGZMessageServer2Ptr pMS2;
    if (!pMS2) {
        LOG_ERROR("Cannot unregister shortcuts: Message server unavailable");
        return;
    }

    // Unregister message notifications
    for (uint32_t messageID : messageIDs) {
        if (!pMS2->RemoveNotification(messageTarget, messageID)) {
            LOG_WARN("Failed to unregister notification for message ID 0x{:08X}", messageID);
        } else {
            LOG_DEBUG("Unregistered notification for message ID 0x{:08X}", messageID);
        }
    }

    LOG_INFO("Shortcuts unregistered successfully");
}