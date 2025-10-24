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
#include "IconLoader.h"
#include "DX11ImageLoader.h"
#include "../exemplar/IconResourceUtil.h"
#include <vector>

bool IconLoader::LoadIconFromPNG(
    cIGZPersistResourceManager* pRM,
    uint32_t iconInstance,
    ID3D11Device* pDevice,
    ID3D11ShaderResourceView** outSRV,
    int* outWidth,
    int* outHeight
) {
    if (!pRM || !pDevice || !outSRV || iconInstance == 0) {
        return false;
    }

    // Load PNG bytes from resource
    std::vector<uint8_t> pngBytes;
    if (!ExemplarUtil::LoadPNGByInstance(pRM, iconInstance, pngBytes) || pngBytes.empty()) {
        return false;
    }

    // Convert to D3D11 texture
    ID3D11ShaderResourceView* srv = nullptr;
    int w = 0, h = 0;
    if (!gfx::CreateSRVFromPNGMemory(pngBytes.data(), pngBytes.size(), pDevice, &srv, &w, &h)) {
        return false;
    }

    *outSRV = srv;
    if (outWidth) *outWidth = w;
    if (outHeight) *outHeight = h;
    return true;
}
