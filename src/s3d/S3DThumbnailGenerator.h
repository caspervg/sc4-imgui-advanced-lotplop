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
#include <d3d11.h>

// Forward declarations
class cISCPropertyHolder;
class cIGZPersistResourceManager;

namespace S3D {

/**
 * Utility class for generating S3D thumbnails from building exemplars.
 *
 * This class provides functionality to:
 * - Extract S3D resource keys from building exemplars (via RKT0-RKT5 properties)
 * - Load and parse S3D models
 * - Generate thumbnail textures suitable for UI display
 *
 * Supported Resource Key Types (RKT):
 * - RKT0: Single model for all zoom levels and rotations
 * - RKT1: Calculated instance per zoom/rotation (most common)
 * - RKT2: Explicit instance per zoom/rotation (20 instances)
 * - RKT3: Explicit instance per zoom level (5 instances, rotation ignored)
 * - RKT4: Multi-model, multi-state (not yet supported)
 * - RKT5: Calculated instance per zoom/rotation with state support
 *
 * Architecture:
 * - Static methods for stateless operations
 * - Handles zoom level and rotation selection for optimal thumbnail view
 * - Returns D3D11 shader resource views compatible with ImGui
 */
class ThumbnailGenerator {
public:
    /**
     * Generates a thumbnail from a building exemplar's S3D model.
     *
     * Process:
     * 1. Extract RKT property from building exemplar to get S3D resource key
     * 2. Calculate appropriate instance based on RKT type and zoom/rotation
     *    - RKT0: Use base instance directly
     *    - RKT1/RKT5: Calculate with zoom/rotation offsets
     *    - RKT2: Look up explicit instance for zoom/rotation
     *    - RKT3: Look up explicit instance for zoom level
     * 3. Load and parse S3D model from resource manager
     * 4. Render model to thumbnail texture
     *
     * @param pBuildingExemplar The building exemplar containing RKT property
     * @param pRM Resource manager for loading S3D data
     * @param pDevice D3D11 device for texture creation
     * @param pContext D3D11 context for rendering
     * @param thumbnailSize Desired thumbnail dimension (square, e.g., 64 for 64x64)
     * @param zoomLevel SC4 zoom level (1=farthest, 5=closest), default 5 for best detail
     * @param rotation SC4 rotation (0-3 for S,E,N,W), default 0 for south view
     * @return Shader resource view for the thumbnail, or nullptr on failure
     *         Caller owns the returned SRV and must Release() it when done
     */
    static ID3D11ShaderResourceView* GenerateThumbnailFromExemplar(
        cISCPropertyHolder* pBuildingExemplar,
        cIGZPersistResourceManager* pRM,
        ID3D11Device* pDevice,
        ID3D11DeviceContext* pContext,
        int thumbnailSize = 64,
        int zoomLevel = 5,
        int rotation = 0
    );

    /**
     * Extracts S3D resource key from building exemplar's RKT properties.
     *
     * Supports multiple Resource Key Type properties (RKT0-RKT5):
     * - RKT0 (0x27812820): 1 model for all zoom/rotation (3 values: Type, Group, Instance)
     * - RKT1 (0x27812821): 1 model per zoom/rotation (3 values: Type, Group, base Instance)
     * - RKT2 (0x27812822): Unique instance per zoom/rotation (20 values: Type, Group, 20 instances)
     * - RKT3 (0x27812823): Unique instance per zoom (7 values: Type, Group, 5 instances)
     * - RKT4 (0x27812824): Multi-model, multi-state (variable count)
     * - RKT5 (0x27812825): 1 model per zoom/rotation/state (3 values: Type, Group, base Instance)
     *
     * This function tries RKT properties in priority order (1, 0, 2, 3) and returns
     * the base instance. For RKT1/RKT5, instance is calculated with zoom/rotation offsets.
     *
     * @param pBuildingExemplar The building exemplar to query
     * @param outType Output parameter for S3D resource type
     * @param outGroup Output parameter for S3D resource group
     * @param outInstance Output parameter for base S3D resource instance
     * @return true if any RKT property exists and was successfully parsed
     */
    static bool GetS3DResourceKey(
        cISCPropertyHolder* pBuildingExemplar,
        uint32_t& outType,
        uint32_t& outGroup,
        uint32_t& outInstance
    );

    /**
     * Calculates final S3D instance ID from base instance with zoom/rotation offsets.
     *
     * SC4 uses a pattern: instance = base + (zoom-1)*0x100 + rotation*0x10
     * - Zoom levels: 1-5 (1=farthest, 5=closest)
     * - Rotations: 0-3 (South, East, North, West cardinal directions)
     *
     * @param baseInstance Base instance from RKT1 property
     * @param zoomLevel Zoom level (1-5)
     * @param rotation Rotation (0-3)
     * @return Final instance ID for the S3D resource
     */
    static uint32_t CalculateS3DInstance(
        uint32_t baseInstance,
        int zoomLevel,
        int rotation
    );
};

} // namespace S3D

