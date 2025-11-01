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

#include "cIGZPersistResourceManager.h"
#include "cIGZString.h"
#include "cISCPropertyHolder.h"
#include "cRZAutoRefCount.h"

/**
 * Retrieves an exemplar (property holder) by its instance ID.
 *
 * @param pRM Resource manager to search in
 * @param instanceID The instance ID of the exemplar to retrieve
 * @param outExemplar Output parameter for the retrieved exemplar
 * @return true if exemplar was found and loaded, false otherwise
 */
bool GetExemplarByInstance(
    cIGZPersistResourceManager* pRM,
    uint32_t instanceID,
    cRZAutoRefCount<cISCPropertyHolder>& outExemplar
);

/**
 * Retrieves an exemplar by instance ID that also matches a specific exemplar type.
 * This is useful for filtering exemplars when multiple resources share the same instance ID.
 *
 * @param pRM Resource manager to search in
 * @param instanceID The instance ID to search for
 * @param exemplarTypePropertyID Property ID for the exemplar type (e.g., kPropertyExemplarType)
 * @param expectedTypeValue Expected value of the exemplar type property
 * @param outExemplar Output parameter for the retrieved exemplar
 * @return true if matching exemplar was found, false otherwise
 */
bool GetExemplarByInstanceAndType(
    cIGZPersistResourceManager* pRM,
    uint32_t instanceID,
    uint32_t exemplarTypePropertyID,
    uint32_t expectedTypeValue,
    cRZAutoRefCount<cISCPropertyHolder>& outExemplar
);

/**
 * Extracts the building exemplar ID from a lot exemplar's PropertyLotObjects.
 * Searches through the PropertyLotObjects property range (0x88EDC900 - 0x88EDC9FF)
 * to find the first building object (type 0) and returns its exemplar ID.
 *
 * @param pLotExemplar The lot exemplar to search
 * @param outBuildingExemplarID Output parameter for the building exemplar ID
 * @return true if a building exemplar ID was found, false otherwise
 */
bool GetLotBuildingExemplarID(
    cISCPropertyHolder* pLotExemplar,
    uint32_t& outBuildingExemplarID
);

/**
 * Gets the localized name of a building from its exemplar.
 * Looks up the UserVisibleNameKey property and retrieves the localized string.
 *
 * @param pRM Resource manager for string lookups
 * @param pBuildingExemplar The building exemplar
 * @param outName Output parameter for the localized name
 * @return true if name was successfully retrieved, false otherwise
 */
bool GetLocalizedBuildingName(
    cIGZPersistResourceManager* pRM,
    cISCPropertyHolder* pBuildingExemplar,
    cRZAutoRefCount<cIGZString>& outName
);

/**
 * Gets a property value as uint32 from a property holder.
 *
 * @param pHolder The property holder to query
 * @param propertyID The property ID to look up
 * @param outValue Output parameter for the property value
 * @return true if property exists and is a uint32, false otherwise
 */
bool GetPropertyUint32(
    cISCPropertyHolder* pHolder,
    uint32_t propertyID,
    uint32_t& outValue
);

/**
 * Gets a property value as uint32 array from a property holder.
 *
 * @param pHolder The property holder to query
 * @param propertyID The property ID to look up
 * @param outData Output pointer to the uint32 array data
 * @param outCount Output parameter for the array element count
 * @return true if property exists and is a uint32 array, false otherwise
 */
bool GetPropertyUint32Array(
    cISCPropertyHolder* pHolder,
    uint32_t propertyID,
    const uint32_t*& outData,
    uint32_t& outCount
);