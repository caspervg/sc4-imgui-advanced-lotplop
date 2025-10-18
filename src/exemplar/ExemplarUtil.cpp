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
// ReSharper disable CppDFAUnreachableCode
// ReSharper disable CppDFAConstantFunctionResult
#include "ExemplarUtil.h"
#include "PersistResourceKeyFilterByInstance.h"
#include "cIGZPersistResourceKeyList.h"
#include "cIGZVariant.h"
#include "cISCProperty.h"
#include "StringResourceKey.h"
#include "StringResourceManager.h"
#include "SCPropertyUtil.h"

// Common property IDs
static constexpr uint32_t kPropertyLotObjectsBase = 0x88EDC900;
static constexpr uint32_t kPropertyLotObjectsRange = 256;
static constexpr uint32_t kPropertyUserVisibleNameKey = 0x8A416A99;

bool GetExemplarByInstance(
    cIGZPersistResourceManager* pRM,
    uint32_t instanceID,
    cRZAutoRefCount<cISCPropertyHolder>& outExemplar
) {
    if (!pRM) return false;

    const cRZAutoRefCount filter(
        new PersistResourceKeyFilterByInstance(instanceID),
        cRZAutoRefCount<PersistResourceKeyFilterByInstance>::kAddRef
    );

    cRZAutoRefCount<cIGZPersistResourceKeyList> pKeyList;
    if (pRM->GetAvailableResourceList(pKeyList.AsPPObj(), filter) > 0 && pKeyList) {
        return pRM->GetResource(
            pKeyList->GetKey(0),
            GZIID_cISCPropertyHolder,
            outExemplar.AsPPVoid(),
            0,
            nullptr
        );
    }

    return false;
}

bool GetExemplarByInstanceAndType(
    cIGZPersistResourceManager* pRM,
    uint32_t instanceID,
    uint32_t exemplarTypePropertyID,
    uint32_t expectedTypeValue,
    cRZAutoRefCount<cISCPropertyHolder>& outExemplar
) {
    if (!pRM) return false;

    cRZAutoRefCount filter(
        new PersistResourceKeyFilterByInstance(instanceID),
        cRZAutoRefCount<PersistResourceKeyFilterByInstance>::kAddRef
    );

    cRZAutoRefCount<cIGZPersistResourceKeyList> pKeyList;
    if (pRM->GetAvailableResourceList(pKeyList.AsPPObj(), filter) > 0 && pKeyList) {
        // Iterate through all resources with this instance ID
        for (auto i = 0; i < pKeyList->Size(); i++) {
            cRZAutoRefCount<cISCPropertyHolder> pPotentialExemplar;
            if (pRM->GetResource(
                pKeyList->GetKey(i),
                GZIID_cISCPropertyHolder,
                pPotentialExemplar.AsPPVoid(),
                0,
                nullptr
            )) {
                // Check if it matches the expected type
                const cISCProperty* pTypeProp = pPotentialExemplar->GetProperty(exemplarTypePropertyID);
                if (!pTypeProp) continue;

                const cIGZVariant* pTypeVariant = pTypeProp->GetPropertyValue();
                if (!pTypeVariant || pTypeVariant->GetType() != cIGZVariant::Uint32) continue;

                if (pTypeVariant->GetValUint32() == expectedTypeValue) {
                    outExemplar = pPotentialExemplar;
                    return true;
                }
            }
        }
    }

    return false;
}

bool GetLotBuildingExemplarID(
    cISCPropertyHolder* pLotExemplar,
    uint32_t& outBuildingExemplarID
) {
    if (!pLotExemplar) return false;

    // Search through PropertyLotObjects range
    for (uint32_t propID = kPropertyLotObjectsBase;
         propID < kPropertyLotObjectsBase + kPropertyLotObjectsRange;
         propID++) {

        const cISCProperty* pProp = pLotExemplar->GetProperty(propID);
        if (!pProp) continue;

        const cIGZVariant* pVariant = pProp->GetPropertyValue();
        if (!pVariant ||
            pVariant->GetType() != cIGZVariant::Uint32Array ||
            pVariant->GetCount() < 13) {
            continue;
        }

        const uint32_t* pData = pVariant->RefUint32();

        // Type 0 = building object
        if (pData[0] != 0) continue;

        uint32_t buildingExemplarID = pData[12];
        if (buildingExemplarID == 0) break;

        outBuildingExemplarID = buildingExemplarID;
        return true;
    }

    return false;
}

bool GetLocalizedBuildingName(
    cIGZPersistResourceManager* pRM,
    cISCPropertyHolder* pBuildingExemplar,
    cRZAutoRefCount<cIGZString>& outName
) {
    if (!pRM || !pBuildingExemplar) return false;

    StringResourceKey nameKey;
    if (SCPropertyUtil::GetPropertyValue(pBuildingExemplar, kPropertyUserVisibleNameKey, nameKey)) {
        return StringResourceManager::GetLocalizedString(nameKey, outName);
    }

    return false;
}

bool GetPropertyUint32(
    cISCPropertyHolder* pHolder,
    uint32_t propertyID,
    uint32_t& outValue
) {
    if (!pHolder) return false;

    const cISCProperty* pProp = pHolder->GetProperty(propertyID);
    if (!pProp) return false;

    const cIGZVariant* pVariant = pProp->GetPropertyValue();
    if (!pVariant || pVariant->GetType() != cIGZVariant::Uint32) return false;

    outValue = pVariant->GetValUint32();
    return true;
}

bool GetPropertyUint32Array(
    cISCPropertyHolder* pHolder,
    uint32_t propertyID,
    const uint32_t*& outData,
    uint32_t& outCount
) {
    if (!pHolder) return false;

    const cISCProperty* pProp = pHolder->GetProperty(propertyID);
    if (!pProp) return false;

    const cIGZVariant* pVariant = pProp->GetPropertyValue();
    if (!pVariant || pVariant->GetType() != cIGZVariant::Uint32Array) return false;

    outData = pVariant->RefUint32();
    outCount = pVariant->GetCount();
    return true;
}