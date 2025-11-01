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
#include "S3DThumbnailGenerator.h"
#include "S3DReader.h"
#include "S3DRenderer.h"
#include "cGZPersistResourceKey.h"
#include "cIGZPersistDBRecord.h"
#include "cIGZPersistResourceManager.h"
#include "cIGZVariant.h"
#include "cISCProperty.h"
#include "cISCPropertyHolder.h"
#include "../utils/Logger.h"
#include <vector>

namespace S3D {

bool ThumbnailGenerator::GetS3DResourceKey(
    cISCPropertyHolder* pBuildingExemplar,
    uint32_t& outType,
    uint32_t& outGroup,
    uint32_t& outInstance
) {
    if (!pBuildingExemplar) {
        return false;
    }

    // Resource Key Type property IDs
    constexpr uint32_t kResourceKeyType0 = 0x27812820; // 1 model for all Z/R
    constexpr uint32_t kResourceKeyType1 = 0x27812821; // 1 model per Z/R (calculated)
    constexpr uint32_t kResourceKeyType2 = 0x27812822; // Unique instance per Z/R (20 values)
    constexpr uint32_t kResourceKeyType3 = 0x27812823; // Unique instance per zoom (7 values)
    constexpr uint32_t kResourceKeyType4 = 0x27812824; // Multi-model, multi-state
    constexpr uint32_t kResourceKeyType5 = 0x27812825; // 1 model per Z/R/state (calculated)

    // Try RKT1 first (most common for buildings)
    const cISCProperty* prop = pBuildingExemplar->GetProperty(kResourceKeyType1);
    if (prop) {
        const cIGZVariant* val = prop->GetPropertyValue();
        if (val && val->GetType() == cIGZVariant::Type::Uint32Array && val->GetCount() >= 3) {
            const uint32_t* vals = val->RefUint32();
            if (vals) {
                outType = vals[0];
                outGroup = vals[1];
                outInstance = vals[2];
                LOG_DEBUG("S3D: Found RKT1 property - TGI {:08X}-{:08X}-{:08X}",
                          outType, outGroup, outInstance);
                return true;
            }
        }
    }

    // Try RKT0 (single model for all zoom/rotation)
    prop = pBuildingExemplar->GetProperty(kResourceKeyType0);
    if (prop) {
        const cIGZVariant* val = prop->GetPropertyValue();
        if (val && val->GetType() == cIGZVariant::Type::Uint32Array && val->GetCount() >= 3) {
            const uint32_t* vals = val->RefUint32();
            if (vals) {
                outType = vals[0];
                outGroup = vals[1];
                outInstance = vals[2];
                LOG_DEBUG("S3D: Found RKT0 property - TGI {:08X}-{:08X}-{:08X}",
                          outType, outGroup, outInstance);
                return true;
            }
        }
    }

    // Try RKT2 (unique instance per zoom/rotation - 20 values)
    // Format: [Type, Group, Instance_Z1R0, Instance_Z1R1, ..., Instance_Z5R3]
    prop = pBuildingExemplar->GetProperty(kResourceKeyType2);
    if (prop) {
        const cIGZVariant* val = prop->GetPropertyValue();
        if (val && val->GetType() == cIGZVariant::Type::Uint32Array && val->GetCount() >= 3) {
            const uint32_t* vals = val->RefUint32();
            if (vals) {
                outType = vals[0];
                outGroup = vals[1];
                // Use the first instance as base (Z1R0)
                outInstance = vals[2];
                LOG_DEBUG("S3D: Found RKT2 property - TGI {:08X}-{:08X}-{:08X} (base)",
                          outType, outGroup, outInstance);
                return true;
            }
        }
    }

    // Try RKT3 (unique instance per zoom - 7 values)
    // Format: [Type, Group, Instance_Z1, Instance_Z2, ..., Instance_Z5]
    prop = pBuildingExemplar->GetProperty(kResourceKeyType3);
    if (prop) {
        const cIGZVariant* val = prop->GetPropertyValue();
        if (val && val->GetType() == cIGZVariant::Type::Uint32Array && val->GetCount() >= 3) {
            const uint32_t* vals = val->RefUint32();
            if (vals) {
                outType = vals[0];
                outGroup = vals[1];
                // Use the first instance as base (Z1)
                outInstance = vals[2];
                LOG_DEBUG("S3D: Found RKT3 property - TGI {:08X}-{:08X}-{:08X} (base)",
                          outType, outGroup, outInstance);
                return true;
            }
        }
    }

    // Try RKT5 (similar to RKT1 but with state support)
    prop = pBuildingExemplar->GetProperty(kResourceKeyType5);
    if (prop) {
        const cIGZVariant* val = prop->GetPropertyValue();
        if (val && val->GetType() == cIGZVariant::Type::Uint32Array && val->GetCount() >= 3) {
            const uint32_t* vals = val->RefUint32();
            if (vals) {
                outType = vals[0];
                outGroup = vals[1];
                outInstance = vals[2];
                LOG_DEBUG("S3D: Found RKT5 property - TGI {:08X}-{:08X}-{:08X}",
                          outType, outGroup, outInstance);
                return true;
            }
        }
    }

    // RKT4 is not supported yet (multi-model, multi-state with variable count)
    prop = pBuildingExemplar->GetProperty(kResourceKeyType4);
    if (prop) {
        LOG_DEBUG("S3D: Found RKT4 property but it's not yet supported");
    }

    return false;
}

uint32_t ThumbnailGenerator::CalculateS3DInstance(
    uint32_t baseInstance,
    int zoomLevel,
    int rotation
) {
    // Clamp values to valid ranges
    if (zoomLevel < 1) zoomLevel = 1;
    if (zoomLevel > 5) zoomLevel = 5;
    if (rotation < 0) rotation = 0;
    if (rotation > 3) rotation = 3;

    // SC4 pattern: instance = base + (zoom-1)*0x100 + rotation*0x10
    uint32_t zoomOffset = static_cast<uint32_t>(zoomLevel - 1) * 0x100;
    uint32_t rotationOffset = static_cast<uint32_t>(rotation) * 0x10;

    return baseInstance + zoomOffset + rotationOffset;
}

ID3D11ShaderResourceView* ThumbnailGenerator::GenerateThumbnailFromExemplar(
    cISCPropertyHolder* pBuildingExemplar,
    cIGZPersistResourceManager* pRM,
    ID3D11Device* pDevice,
    ID3D11DeviceContext* pContext,
    int thumbnailSize,
    int zoomLevel,
    int rotation
) {
    if (!pBuildingExemplar || !pRM || !pDevice || !pContext) {
        LOG_DEBUG("S3D thumbnail: Invalid parameters");
        return nullptr;
    }

    // Extract S3D resource key from building exemplar
    uint32_t s3dType = 0;
    uint32_t s3dGroup = 0;
    uint32_t baseInstance = 0;

    if (!GetS3DResourceKey(pBuildingExemplar, s3dType, s3dGroup, baseInstance)) {
        LOG_DEBUG("S3D thumbnail: No RKT property found in building exemplar");
        return nullptr;
    }

    // Determine which RKT type was found and calculate final instance accordingly
    uint32_t finalInstance = baseInstance;

    // Resource Key Type property IDs
    constexpr uint32_t kResourceKeyType0 = 0x27812820;
    constexpr uint32_t kResourceKeyType1 = 0x27812821;
    constexpr uint32_t kResourceKeyType2 = 0x27812822;
    constexpr uint32_t kResourceKeyType3 = 0x27812823;
    constexpr uint32_t kResourceKeyType5 = 0x27812825;

    // Check which RKT type is present
    const cISCProperty* rktProp = nullptr;

    if ((rktProp = pBuildingExemplar->GetProperty(kResourceKeyType1)) != nullptr) {
        // RKT1: Calculate instance with zoom/rotation offsets
        finalInstance = CalculateS3DInstance(baseInstance, zoomLevel, rotation);
        LOG_DEBUG("S3D thumbnail: RKT1 - base=0x{:08X}, zoom={}, rot={}, final=0x{:08X}",
                  baseInstance, zoomLevel, rotation, finalInstance);
    }
    else if ((rktProp = pBuildingExemplar->GetProperty(kResourceKeyType0)) != nullptr) {
        // RKT0: Use base instance directly (same for all zoom/rotation)
        finalInstance = baseInstance;
        LOG_DEBUG("S3D thumbnail: RKT0 - instance=0x{:08X} (same for all Z/R)",
                  finalInstance);
    }
    else if ((rktProp = pBuildingExemplar->GetProperty(kResourceKeyType2)) != nullptr) {
        // RKT2: Has explicit instance for each zoom/rotation
        // Format: [Type, Group, Z1R0, Z1R1, Z1R2, Z1R3, Z2R0, ..., Z5R3]
        const cIGZVariant* val = rktProp->GetPropertyValue();
        if (val && val->GetType() == cIGZVariant::Type::Uint32Array) {
            const uint32_t* vals = val->RefUint32();
            int clampedZoom = (zoomLevel < 1) ? 1 : (zoomLevel > 5) ? 5 : zoomLevel;
            int clampedRot = (rotation < 0) ? 0 : (rotation > 3) ? 3 : rotation;
            // Index: 2 + (zoom-1)*4 + rotation
            int index = 2 + (clampedZoom - 1) * 4 + clampedRot;
            if (index < static_cast<int>(val->GetCount())) {
                finalInstance = vals[index];
                LOG_TRACE("S3D thumbnail: RKT2 - zoom={}, rot={}, index={}, instance=0x{:08X}",
                          clampedZoom, clampedRot, index, finalInstance);
            }
        }
    }
    else if ((rktProp = pBuildingExemplar->GetProperty(kResourceKeyType3)) != nullptr) {
        // RKT3: Has explicit instance for each zoom (rotation ignored)
        // Format: [Type, Group, Z1, Z2, Z3, Z4, Z5]
        const cIGZVariant* val = rktProp->GetPropertyValue();
        if (val && val->GetType() == cIGZVariant::Type::Uint32Array) {
            const uint32_t* vals = val->RefUint32();
            int clampedZoom = (zoomLevel < 1) ? 1 : (zoomLevel > 5) ? 5 : zoomLevel;
            // Index: 2 + (zoom-1)
            int index = 2 + (clampedZoom - 1);
            if (index < static_cast<int>(val->GetCount())) {
                finalInstance = vals[index];
                LOG_TRACE("S3D thumbnail: RKT3 - zoom={}, index={}, instance=0x{:08X}",
                          clampedZoom, index, finalInstance);
            }
        }
    }
    else if ((rktProp = pBuildingExemplar->GetProperty(kResourceKeyType5)) != nullptr) {
        // RKT5: Similar to RKT1 (calculate instance with zoom/rotation)
        finalInstance = CalculateS3DInstance(baseInstance, zoomLevel, rotation);
        LOG_TRACE("S3D thumbnail: RKT5 - base=0x{:08X}, zoom={}, rot={}, final=0x{:08X}",
                  baseInstance, zoomLevel, rotation, finalInstance);
    }

    // Open S3D resource
    cGZPersistResourceKey s3dKey(s3dType, s3dGroup, finalInstance);
    cIGZPersistDBRecord* pRecord = nullptr;

    if (!pRM->OpenDBRecord(s3dKey, &pRecord, false)) {
        LOG_DEBUG("S3D thumbnail: S3D resource not found - TGI {:08X}-{:08X}-{:08X}",
                  s3dType, s3dGroup, finalInstance);
        return nullptr;
    }

    uint32_t dataSize = pRecord->GetSize();
    if (dataSize == 0) {
        LOG_DEBUG("S3D thumbnail: S3D record has zero size");
        pRecord->Close();
        return nullptr;
    }

    // Read S3D data
    std::vector<uint8_t> s3dData(dataSize);
    if (!pRecord->GetFieldVoid(s3dData.data(), dataSize)) {
        LOG_DEBUG("S3D thumbnail: Failed to read S3D data");
        pRecord->Close();
        return nullptr;
    }

    pRecord->Close();

    // Parse S3D model
    S3D::Model model;
    if (!S3D::Reader::Parse(s3dData.data(), dataSize, model)) {
        LOG_DEBUG("S3D thumbnail: Failed to parse S3D model");
        return nullptr;
    }

    LOG_TRACE("S3D thumbnail: Model parsed - {} meshes, {} frames",
              model.animation.animatedMeshes.size(), model.animation.frameCount);

    // Create renderer
    S3D::Renderer renderer(pDevice, pContext);

    // Load model into renderer
    if (!renderer.LoadModel(model, pRM, s3dGroup)) {
        LOG_DEBUG("S3D thumbnail: Failed to load model into renderer");
        return nullptr;
    }

    // Generate thumbnail
    ID3D11ShaderResourceView* thumbnailSRV = renderer.GenerateThumbnail(thumbnailSize);

    if (!thumbnailSRV) {
        LOG_DEBUG("S3D thumbnail: Failed to generate thumbnail texture");
        return nullptr;
    }

    LOG_DEBUG("S3D thumbnail: Successfully generated {}x{} thumbnail", thumbnailSize, thumbnailSize);

    return thumbnailSRV;
}

} // namespace S3D

