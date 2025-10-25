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
#include "DX7ImageLoader.h"
#include "../exemplar/IconResourceUtil.h"
#include <vector>

bool IconLoader::LoadIconFromPNG(
    cIGZPersistResourceManager* pRM,
    uint32_t iconInstance,
    IDirectDraw7* pDDraw,
    IDirectDrawSurface7** outSurface,
    int* outWidth,
    int* outHeight
) {
    if (!pRM || !pDDraw || !outSurface || iconInstance == 0) {
        return false;
    }

    // Load PNG bytes from resource
    std::vector<uint8_t> pngBytes;
    if (!ExemplarUtil::LoadPNGByInstance(pRM, iconInstance, pngBytes) || pngBytes.empty()) {
        return false;
    }

    // Convert to DX7 surface
    IDirectDrawSurface7* surface = nullptr;
    int w = 0, h = 0;
    if (!gfx::CreateSurfaceFromPNGMemory(pngBytes.data(), pngBytes.size(), pDDraw, &surface, &w, &h)) {
        return false;
    }

    *outSurface = surface;
    if (outWidth) *outWidth = w;
    if (outHeight) *outHeight = h;
    return true;
}
