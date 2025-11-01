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

class cIGZPersistResourceManager;
struct ID3D11Device;
struct ID3D11ShaderResourceView;

/**
 * Handles loading and converting PNG icons from SC4 resources to D3D11 textures.
 */
class IconLoader {
public:
    /**
     * Load an icon from a PNG resource instance ID and create a D3D11 shader resource view.
     * @param pRM Resource manager
     * @param iconInstance PNG instance ID
     * @param pDevice D3D11 device
     * @param outSRV Output shader resource view (caller must Release when done)
     * @param outWidth Output icon width
     * @param outHeight Output icon height
     * @return true if successful, false otherwise
     */
    static bool LoadIconFromPNG(
        cIGZPersistResourceManager* pRM,
        uint32_t iconInstance,
        ID3D11Device* pDevice,
        ID3D11ShaderResourceView** outSRV,
        int* outWidth,
        int* outHeight
    );
};
