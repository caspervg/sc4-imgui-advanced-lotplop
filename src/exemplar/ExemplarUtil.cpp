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
#include "PropertyUtil.h"
#include "SCPropertyUtil.h"
#include "PersistResourceKeyFilterByInstance.h"
#include "cIGZPersistResourceKeyList.h"
#include "cIGZVariant.h"
#include "cISCProperty.h"

static constexpr uint32_t kPropertyLotObjectsStart = 0x88EDC900;
static constexpr uint32_t kPropertyLotObjectsEnd = 0x88EDCDFF;

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
        for (auto i = 0; i < pKeyList->Size(); i++) {
            cRZAutoRefCount<cISCPropertyHolder> pPotentialExemplar;
            if (pRM->GetResource(
                pKeyList->GetKey(i),
                GZIID_cISCPropertyHolder,
                pPotentialExemplar.AsPPVoid(),
                0,
                nullptr
            )) {
                uint32_t typeValue;
                if (SCPropertyUtil::GetPropertyValue(pPotentialExemplar, exemplarTypePropertyID, typeValue)
                    && typeValue == expectedTypeValue) {
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
    for (uint32_t propID = kPropertyLotObjectsStart;
         propID <= kPropertyLotObjectsEnd;
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
    if (!pBuildingExemplar) return false;
    return PropertyUtil::GetUserVisibleName(pBuildingExemplar, outName);
}

bool GetPropertyUint32(
    cISCPropertyHolder* pHolder,
    uint32_t propertyID,
    uint32_t& outValue
) {
    return SCPropertyUtil::GetPropertyValue(pHolder, propertyID, outValue);
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