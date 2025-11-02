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
#include <string>
#include <vector>

struct sqlite3;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;
struct LotConfigEntry;

/**
 * @brief Manages persistent caching of lot configurations and S3D thumbnails using SQLite.
 *
 * Stores:
 * - Lot configuration data (name, size, S3D references, occupant groups)
 * - PNG icon data (decoded RGBA from lot menu icons)
 * - S3D model thumbnails (rendered building previews as RGBA)
 *
 * Key features:
 * - Deduplication: One S3D thumbnail per unique model instance
 * - Fast loading: Direct GPU upload of RGBA bytes (~0.1ms vs 2-5ms PNG decode)
 * - Persistence: SQLite database survives game restarts
 * - Version tracking: Invalidate cache when schema changes
 */
class PersistentCache {
public:
    PersistentCache();
    ~PersistentCache();

    // Disable copy/move (manages SQLite connection)
    PersistentCache(const PersistentCache&) = delete;
    PersistentCache& operator=(const PersistentCache&) = delete;

    /**
     * @brief Initialize the cache database
     * @param dbPath Path to SQLite database file
     * @param schemaVersion Current schema version (for migration/invalidation)
     * @return true if initialized successfully
     */
    bool Initialize(const std::string& dbPath, int schemaVersion = 1);

    /**
     * @brief Close the database connection
     */
    void Close();

    /**
     * @brief Check if a thumbnail exists in the cache
     * @param s3dInstance S3D resource instance ID
     * @return true if thumbnail is cached
     */
    bool HasThumbnail(uint32_t s3dInstance);

    /**
     * @brief Save a thumbnail to the cache
     * @param s3dInstance S3D resource instance ID
     * @param s3dType S3D type ID (usually 0x5AD0E817)
     * @param s3dGroup S3D group ID (building family)
     * @param rgbaData Raw RGBA bytes (width * height * 4)
     * @param width Thumbnail width
     * @param height Thumbnail height
     * @param zoomLevel Zoom level used for rendering (1-5)
     * @param rotation Rotation used (0-3)
     * @return true if saved successfully
     */
    bool SaveThumbnail(
        uint32_t s3dInstance,
        uint32_t s3dType,
        uint32_t s3dGroup,
        const std::vector<uint8_t>& rgbaData,
        int width,
        int height,
        int zoomLevel = 5,
        int rotation = 0
    );

    /**
     * @brief Load a thumbnail from the cache
     * @param s3dInstance S3D resource instance ID
     * @param outRGBA Output buffer for RGBA bytes
     * @param outWidth Output thumbnail width
     * @param outHeight Output thumbnail height
     * @return true if loaded successfully
     */
    bool LoadThumbnail(
        uint32_t s3dInstance,
        std::vector<uint8_t>& outRGBA,
        int& outWidth,
        int& outHeight
    );

    /**
     * @brief Load thumbnail and upload to GPU
     * @param s3dInstance S3D resource instance ID
     * @param pDevice D3D11 device
     * @param outSRV Output shader resource view (caller must Release())
     * @param outWidth Output thumbnail width
     * @param outHeight Output thumbnail height
     * @return true if loaded and uploaded successfully
     */
    bool LoadThumbnailToGPU(
        uint32_t s3dInstance,
        ID3D11Device* pDevice,
        ID3D11ShaderResourceView** outSRV,
        int& outWidth,
        int& outHeight
    );

    /**
     * @brief Download GPU texture and save to cache
     * @param s3dInstance S3D resource instance ID
     * @param s3dType S3D type ID
     * @param s3dGroup S3D group ID
     * @param pDevice D3D11 device
     * @param pContext D3D11 device context
     * @param pTexture Source texture (render target)
     * @param zoomLevel Zoom level used
     * @param rotation Rotation used
     * @return true if downloaded and saved successfully
     */
    bool SaveThumbnailFromGPU(
        uint32_t s3dInstance,
        uint32_t s3dType,
        uint32_t s3dGroup,
        ID3D11Device* pDevice,
        ID3D11DeviceContext* pContext,
        ID3D11Texture2D* pTexture,
        int zoomLevel = 5,
        int rotation = 0
    );

    /**
     * @brief Check if database is initialized
     */
    bool IsInitialized() const { return db != nullptr; }

    // ========================================================================
    // Lot Configuration Persistence
    // ========================================================================

    /**
     * @brief Save a lot configuration entry to the cache
     * @param entry Lot configuration entry to save
     * @param iconRGBA Optional RGBA icon data (if PNG icon was decoded)
     * @return true if saved successfully
     */
    bool SaveLotConfig(
        const LotConfigEntry& entry,
        const std::vector<uint8_t>& iconRGBA = {}
    );

    /**
     * @brief Check if a lot configuration exists in the cache
     * @param lotID Lot configuration instance ID
     * @return true if lot config is cached
     */
    bool HasLotConfig(uint32_t lotID);

    /**
     * @brief Load lot configuration metadata (without icon data)
     * @param lotID Lot configuration instance ID
     * @param outEntry Output lot configuration entry (iconSRV will be nullptr)
     * @return true if loaded successfully
     */
    bool LoadLotConfigMetadata(uint32_t lotID, LotConfigEntry& outEntry);

    /**
     * @brief Load lot icon RGBA data and upload to GPU
     * @param lotID Lot configuration instance ID
     * @param pDevice D3D11 device
     * @param outSRV Output shader resource view (caller must Release())
     * @param outWidth Output icon width
     * @param outHeight Output icon height
     * @return true if icon exists and was loaded successfully
     */
    bool LoadLotIconToGPU(
        uint32_t lotID,
        ID3D11Device* pDevice,
        ID3D11ShaderResourceView** outSRV,
        int& outWidth,
        int& outHeight
    );

    /**
     * @brief Get count of cached lot configurations
     * @return Number of lot configs in cache
     */
    int GetLotConfigCount();

    /**
     * @brief Get all lot configuration IDs from the cache
     * @param outLotIDs Output vector of lot IDs
     * @return true if successfully retrieved
     */
    bool GetAllLotConfigIDs(std::vector<uint32_t>& outLotIDs);

private:
    sqlite3* db;
    int currentSchemaVersion;

    // Create database tables if they don't exist
    bool CreateTables();

    // Check and update schema version
    bool CheckSchemaVersion(int expectedVersion);

    // Helper: Upload RGBA bytes to GPU texture
    static bool UploadRGBAToGPU(
        ID3D11Device* pDevice,
        const uint8_t* rgbaData,
        int width,
        int height,
        ID3D11ShaderResourceView** outSRV
    );

    // Helper: Download GPU texture to RGBA bytes
    static bool DownloadRGBAFromGPU(
        ID3D11Device* pDevice,
        ID3D11DeviceContext* pContext,
        ID3D11Texture2D* pTexture,
        std::vector<uint8_t>& outRGBA,
        int& outWidth,
        int& outHeight
    );
};
