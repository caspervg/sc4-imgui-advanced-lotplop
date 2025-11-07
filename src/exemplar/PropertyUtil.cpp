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

#include "PropertyUtil.h"

#include "cIGZVariant.h"
#include "cISCProperty.h"
#include "SCPropertyUtil.h"
#include "StringResourceKey.h"
#include "StringResourceManager.h"

namespace
{
	bool GetUserVisibleNameKey(const cISCPropertyHolder* pPropertyHolder, StringResourceKey& key)
	{
		bool result = false;

		if (pPropertyHolder)
		{
			constexpr uint32_t kUserVisibleNamePropertyID = 0x8A416A99;

			const cISCProperty* userVisibleName = pPropertyHolder->GetProperty(kUserVisibleNamePropertyID);

			if (userVisibleName)
			{
				const cIGZVariant* propertyValue = userVisibleName->GetPropertyValue();

				if (propertyValue->GetType() == cIGZVariant::Type::Uint32Array
					&& propertyValue->GetCount() == 3)
				{
					const uint32_t* pTGI = propertyValue->RefUint32();

					uint32_t group = pTGI[1];
					uint32_t instance = pTGI[2];

					key.groupID = group;
					key.instanceID = instance;
					result = true;
				}
			}
		}

		return result;
	}

	bool TryGetUserVisibleName(
		const cISCPropertyHolder* pPropertyHolder,
		cRZAutoRefCount<cIGZString>& name)
	{
		bool result = false;

		StringResourceKey key;

		if (GetUserVisibleNameKey(pPropertyHolder, key))
		{
			result = StringResourceManager::GetLocalizedString(key, name);
		}

		return result;
	}

	bool TryGetUserVisibleName(
		const cISCPropertyHolder* pPropertyHolder,
		cIGZString& name)
	{
		bool result = false;

		cRZAutoRefCount<cIGZString> userVisibleName;

		if (TryGetUserVisibleName(pPropertyHolder, userVisibleName))
		{
			name.Copy(*userVisibleName);
			result = true;
		}

		return result;
	}
}

bool PropertyUtil::GetExemplarName(const cISCPropertyHolder* pPropertyHolder, cIGZString& name)
{
	constexpr uint32_t kExemplarNameProperty = 0x00000020;

	return SCPropertyUtil::GetPropertyValue(pPropertyHolder, kExemplarNameProperty, name);
}

bool PropertyUtil::GetDisplayName(const cISCPropertyHolder* pPropertyHolder, cIGZString& name)
{
	bool result = false;

	if (pPropertyHolder)
	{
		constexpr uint32_t kItemNameProperty = 0x899AFBAD;

		result = SCPropertyUtil::GetPropertyValue(pPropertyHolder, kItemNameProperty, name);

		if (!result)
		{
			result = TryGetUserVisibleName(pPropertyHolder, name);

			if (!result)
			{
				result = GetExemplarName(pPropertyHolder, name);
			}
		}
	}

	return result;
}

bool PropertyUtil::GetUserVisibleName(
	const cISCPropertyHolder* pPropertyHolder,
	cRZAutoRefCount<cIGZString>& name)
{
	return TryGetUserVisibleName(pPropertyHolder, name);
}

bool PropertyUtil::GetItemDescription(
    const cISCPropertyHolder* pPropertyHolder,
    cIGZString& description)
{
    if (!pPropertyHolder)
        return false;

    bool result = false;

    // 1) Try the localized LTEXT key first
    // Item Description Key (Uint32[3] -> T/G/I, but we use Group+Instance for StringResourceKey)
    constexpr uint32_t kItemDescriptionKeyProperty = 0xCA416AB5;
    StringResourceKey key{};
    if (SCPropertyUtil::GetPropertyValue(pPropertyHolder, kItemDescriptionKeyProperty, key))
    {
        cRZAutoRefCount<cIGZString> localized;
        if (StringResourceManager::GetLocalizedString(key, localized))
        {
            description.Copy(*localized);
            result = true;
        }
    }

    // 2) Fallback to a plain string property if present
    if (!result)
    {
        constexpr uint32_t kItemDescriptionStringProperty = 0x8A2602A9; // Item Description
        result = SCPropertyUtil::GetPropertyValue(pPropertyHolder, kItemDescriptionStringProperty, description);
    }

    return result;
}

bool PropertyUtil::GetPropertyResourceKey(
    const cISCPropertyHolder* pPropertyHolder,
    uint32_t propertyID,
    cGZPersistResourceKey& outKey)
{
    if (!pPropertyHolder)
        return false;

    const cISCProperty* prop = pPropertyHolder->GetProperty(propertyID);
    if (!prop)
        return false;

    const cIGZVariant* val = prop->GetPropertyValue();
    if (!val || val->GetType() != cIGZVariant::Type::Uint32Array)
        return false;

    uint32_t count = val->GetCount();
    if (count < 3)
        return false;

    const uint32_t* vals = val->RefUint32();
    if (!vals)
        return false;

    outKey.type = vals[0];
    outKey.group = vals[1];
    outKey.instance = vals[2];
    return true;
}
