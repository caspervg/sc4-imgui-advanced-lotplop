/*
* This file is part of sc4-query-ui-hooks, a DLL Plugin for SimCity 4 that
 * extends the query UI.
 *
 * Copyright (C) 2024, 2025 Nicholas Hayes
 *
 * sc4-query-ui-hooks is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * sc4-query-ui-hooks is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with sc4-query-ui-hooks.
 * If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once
#include "cIGZString.h"
#include "cISCPropertyHolder.h"
#include "cGZPersistResourceKey.h"
#include "cRZAutoRefCount.h"

namespace PropertyUtil
{
    bool GetExemplarName(
        const cISCPropertyHolder* pPropertyHolder,
        cIGZString& name);

    bool GetDisplayName(
        const cISCPropertyHolder* pPropertyHolder,
        cIGZString& name);

    bool GetUserVisibleName(
        const cISCPropertyHolder* pPropertyHolder,
        cRZAutoRefCount<cIGZString>& name);

    // Resolves Item Description from either a localized LTEXT key or a plain string.
    bool GetItemDescription(
        const cISCPropertyHolder* pPropertyHolder,
        cIGZString& description);

    /**
     * @brief Get a resource key (TGI) from a property
     * @param pPropertyHolder The property holder to query
     * @param propertyID The property ID to read
     * @param outKey Output resource key
     * @return true if successful
     */
    bool GetPropertyResourceKey(
        const cISCPropertyHolder* pPropertyHolder,
        uint32_t propertyID,
        cGZPersistResourceKey& outKey);
};
