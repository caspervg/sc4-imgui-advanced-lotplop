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
#include "PersistentCache.h"
#include "../utils/Logger.h"
#include "../ui/LotConfigEntry.h"

#include <sqlite3.h>
#include <d3d11.h>
#include <ctime>
#include <sstream>

PersistentCache::PersistentCache()
    : db(nullptr), currentSchemaVersion(0) {
}

PersistentCache::~PersistentCache() {
    Close();
}

bool PersistentCache::Initialize(const std::string& dbPath, int schemaVersion) {
    if (db) {
        LOG_WARN("PersistentCache already initialized");
        return true;
    }

    // Open/create database
    int rc = sqlite3_open(dbPath.c_str(), &db);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to open S3D thumbnail cache database: {}", sqlite3_errmsg(db));
        sqlite3_close(db);
        db = nullptr;
        return false;
    }

    LOG_INFO("Opened S3D thumbnail cache database: {}", dbPath);

    // Enable Write-Ahead Logging for better concurrency
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    // Enable foreign keys
    sqlite3_exec(db, "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr);

    // Create tables if needed
    if (!CreateTables()) {
        LOG_ERROR("Failed to create database tables");
        Close();
        return false;
    }

    // Check/update schema version
    currentSchemaVersion = schemaVersion;
    if (!CheckSchemaVersion(schemaVersion)) {
        LOG_WARN("Schema version mismatch, clearing cache");
        sqlite3_exec(db, "DELETE FROM s3d_thumbnails", nullptr, nullptr, nullptr);
    }

    LOG_INFO("S3D thumbnail cache initialized (schema version {})", schemaVersion);
    return true;
}

void PersistentCache::Close() {
    if (db) {
        sqlite3_close(db);
        db = nullptr;
        LOG_INFO("S3D thumbnail cache closed");
    }
}

bool PersistentCache::CreateTables() {
    if (!db) return false;

    const char* sql = R"(
        -- Cache metadata
        CREATE TABLE IF NOT EXISTS cache_metadata (
            key TEXT PRIMARY KEY,
            value TEXT
        );

        -- S3D thumbnails (standalone, no foreign keys)
        CREATE TABLE IF NOT EXISTS s3d_thumbnails (
            s3d_instance INTEGER PRIMARY KEY,
            s3d_type INTEGER NOT NULL,
            s3d_group INTEGER NOT NULL,
            thumbnail_data BLOB NOT NULL,
            width INTEGER NOT NULL,
            height INTEGER NOT NULL,
            zoom_level INTEGER DEFAULT 5,
            rotation INTEGER DEFAULT 0,
            generated_at INTEGER NOT NULL
        );

        -- Lot configurations (with optional PNG icon cache)
        CREATE TABLE IF NOT EXISTS lot_configs (
            lot_id INTEGER PRIMARY KEY,
            name TEXT NOT NULL,
            description TEXT,
            size_x INTEGER NOT NULL,
            size_z INTEGER NOT NULL,
            building_exemplar_id INTEGER,
            s3d_instance INTEGER,
            s3d_type INTEGER,
            s3d_group INTEGER,
            icon_instance INTEGER,
            icon_data BLOB,
            icon_width INTEGER,
            icon_height INTEGER,
            occupant_groups TEXT,
            created_at INTEGER NOT NULL
        );

        CREATE INDEX IF NOT EXISTS idx_lot_size ON lot_configs(size_x, size_z);
        CREATE INDEX IF NOT EXISTS idx_lot_s3d ON lot_configs(s3d_instance);
    )";

    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errMsg);

    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to create tables: {}", errMsg);
        sqlite3_free(errMsg);
        return false;
    }

    return true;
}

bool PersistentCache::CheckSchemaVersion(int expectedVersion) {
    if (!db) return false;

    // Get current schema version
    const char* selectSQL = "SELECT value FROM cache_metadata WHERE key = 'schema_version'";
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(db, selectSQL, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare schema version query: {}", sqlite3_errmsg(db));
        return false;
    }

    int storedVersion = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* versionStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (versionStr) {
            storedVersion = std::atoi(versionStr);
        }
    }
    sqlite3_finalize(stmt);

    // Update schema version if different
    if (storedVersion != expectedVersion) {
        const char* upsertSQL = "INSERT OR REPLACE INTO cache_metadata (key, value) VALUES ('schema_version', ?)";
        rc = sqlite3_prepare_v2(db, upsertSQL, -1, &stmt, nullptr);
        if (rc == SQLITE_OK) {
            std::string versionStr = std::to_string(expectedVersion);
            sqlite3_bind_text(stmt, 1, versionStr.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }

        return storedVersion == 0; // Return true if first init, false if version changed
    }

    return true;
}

bool PersistentCache::HasThumbnail(uint32_t s3dInstance) {
    if (!db) return false;

    const char* sql = "SELECT 1 FROM s3d_thumbnails WHERE s3d_instance = ? LIMIT 1";
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare HasThumbnail query: {}", sqlite3_errmsg(db));
        return false;
    }

    sqlite3_bind_int(stmt, 1, static_cast<int>(s3dInstance));
    bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);

    return exists;
}

bool PersistentCache::SaveThumbnail(
    uint32_t s3dInstance,
    uint32_t s3dType,
    uint32_t s3dGroup,
    const std::vector<uint8_t>& rgbaData,
    int width,
    int height,
    int zoomLevel,
    int rotation) {

    if (!db) {
        LOG_ERROR("Database not initialized");
        return false;
    }

    if (rgbaData.empty()) {
        LOG_ERROR("Empty RGBA data");
        return false;
    }

    const char* sql = R"(
        INSERT OR REPLACE INTO s3d_thumbnails
        (s3d_instance, s3d_type, s3d_group, thumbnail_data, width, height,
         zoom_level, rotation, generated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare SaveThumbnail statement: {}", sqlite3_errmsg(db));
        return false;
    }

    int64_t timestamp = static_cast<int64_t>(std::time(nullptr));

    sqlite3_bind_int(stmt, 1, static_cast<int>(s3dInstance));
    sqlite3_bind_int(stmt, 2, static_cast<int>(s3dType));
    sqlite3_bind_int(stmt, 3, static_cast<int>(s3dGroup));
    sqlite3_bind_blob(stmt, 4, rgbaData.data(), static_cast<int>(rgbaData.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, width);
    sqlite3_bind_int(stmt, 6, height);
    sqlite3_bind_int(stmt, 7, zoomLevel);
    sqlite3_bind_int(stmt, 8, rotation);
    sqlite3_bind_int64(stmt, 9, timestamp);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        LOG_ERROR("Failed to save thumbnail for S3D 0x{:08X}: {}", s3dInstance, sqlite3_errmsg(db));
        return false;
    }

    LOG_DEBUG("Saved thumbnail for S3D 0x{:08X} ({}x{}, {} bytes)",
              s3dInstance, width, height, rgbaData.size());
    return true;
}

bool PersistentCache::LoadThumbnail(
    uint32_t s3dInstance,
    std::vector<uint8_t>& outRGBA,
    int& outWidth,
    int& outHeight) {

    if (!db) return false;

    const char* sql = "SELECT thumbnail_data, width, height FROM s3d_thumbnails WHERE s3d_instance = ?";
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare LoadThumbnail query: {}", sqlite3_errmsg(db));
        return false;
    }

    sqlite3_bind_int(stmt, 1, static_cast<int>(s3dInstance));

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        // Get BLOB data
        const void* blobData = sqlite3_column_blob(stmt, 0);
        int blobSize = sqlite3_column_bytes(stmt, 0);
        outWidth = sqlite3_column_int(stmt, 1);
        outHeight = sqlite3_column_int(stmt, 2);

        if (blobData && blobSize > 0) {
            outRGBA.resize(blobSize);
            std::memcpy(outRGBA.data(), blobData, blobSize);
            sqlite3_finalize(stmt);
            return true;
        }
    }

    sqlite3_finalize(stmt);
    return false;
}

bool PersistentCache::LoadThumbnailToGPU(
    uint32_t s3dInstance,
    ID3D11Device* pDevice,
    ID3D11ShaderResourceView** outSRV,
    int& outWidth,
    int& outHeight) {

    if (!pDevice || !outSRV) return false;

    std::vector<uint8_t> rgbaData;
    if (!LoadThumbnail(s3dInstance, rgbaData, outWidth, outHeight)) {
        return false;
    }

    return UploadRGBAToGPU(pDevice, rgbaData.data(), outWidth, outHeight, outSRV);
}

bool PersistentCache::SaveThumbnailFromGPU(
    uint32_t s3dInstance,
    uint32_t s3dType,
    uint32_t s3dGroup,
    ID3D11Device* pDevice,
    ID3D11DeviceContext* pContext,
    ID3D11Texture2D* pTexture,
    int zoomLevel,
    int rotation) {

    if (!pDevice || !pContext || !pTexture) return false;

    std::vector<uint8_t> rgbaData;
    int width, height;

    if (!DownloadRGBAFromGPU(pDevice, pContext, pTexture, rgbaData, width, height)) {
        return false;
    }

    return SaveThumbnail(s3dInstance, s3dType, s3dGroup, rgbaData, width, height, zoomLevel, rotation);
}

// ============================================================================
// Static Helper Methods
// ============================================================================

bool PersistentCache::UploadRGBAToGPU(
    ID3D11Device* pDevice,
    const uint8_t* rgbaData,
    int width,
    int height,
    ID3D11ShaderResourceView** outSRV) {

    if (!pDevice || !rgbaData || !outSRV) return false;

    // Create texture description
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;

    // Initial data
    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = rgbaData;
    initData.SysMemPitch = width * 4; // RGBA = 4 bytes per pixel

    // Create texture
    ID3D11Texture2D* pTexture = nullptr;
    HRESULT hr = pDevice->CreateTexture2D(&desc, &initData, &pTexture);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create texture from RGBA data: 0x{:08X}", hr);
        return false;
    }

    // Create shader resource view
    hr = pDevice->CreateShaderResourceView(pTexture, nullptr, outSRV);
    pTexture->Release();

    if (FAILED(hr)) {
        LOG_ERROR("Failed to create SRV from texture: 0x{:08X}", hr);
        return false;
    }

    return true;
}

bool PersistentCache::DownloadRGBAFromGPU(
    ID3D11Device* pDevice,
    ID3D11DeviceContext* pContext,
    ID3D11Texture2D* pTexture,
    std::vector<uint8_t>& outRGBA,
    int& outWidth,
    int& outHeight) {

    if (!pDevice || !pContext || !pTexture) return false;

    // Get texture description
    D3D11_TEXTURE2D_DESC desc;
    pTexture->GetDesc(&desc);
    outWidth = desc.Width;
    outHeight = desc.Height;

    // Create staging texture (CPU-readable)
    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;

    ID3D11Texture2D* pStaging = nullptr;
    HRESULT hr = pDevice->CreateTexture2D(&stagingDesc, nullptr, &pStaging);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create staging texture: 0x{:08X}", hr);
        return false;
    }

    // Copy GPU texture to staging texture
    pContext->CopyResource(pStaging, pTexture);

    // Map and read pixels
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = pContext->Map(pStaging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to map staging texture: 0x{:08X}", hr);
        pStaging->Release();
        return false;
    }

    // Copy data (handle potential row pitch padding)
    size_t pixelCount = desc.Width * desc.Height;
    outRGBA.resize(pixelCount * 4);

    if (mapped.RowPitch == desc.Width * 4) {
        // No padding, direct copy
        std::memcpy(outRGBA.data(), mapped.pData, pixelCount * 4);
    } else {
        // Row padding present, copy row by row
        const uint8_t* srcRow = static_cast<const uint8_t*>(mapped.pData);
        uint8_t* dstRow = outRGBA.data();
        size_t rowBytes = desc.Width * 4;

        for (uint32_t y = 0; y < desc.Height; ++y) {
            std::memcpy(dstRow, srcRow, rowBytes);
            srcRow += mapped.RowPitch;
            dstRow += rowBytes;
        }
    }

    pContext->Unmap(pStaging, 0);
    pStaging->Release();

    return true;
}

// ============================================================================
// Lot Configuration Persistence
// ============================================================================

bool PersistentCache::SaveLotConfig(
    const LotConfigEntry& entry,
    const std::vector<uint8_t>& iconRGBA) {

    if (!db) {
        LOG_ERROR("Database not initialized");
        return false;
    }

    // Serialize occupant groups to comma-separated string
    std::ostringstream oss;
    bool first = true;
    for (uint32_t groupID : entry.occupantGroups) {
        if (!first) oss << ",";
        oss << std::hex << groupID;
        first = false;
    }
    std::string occupantGroupsStr = oss.str();

    const char* sql = R"(
        INSERT OR REPLACE INTO lot_configs
        (lot_id, name, description, size_x, size_z, building_exemplar_id,
         s3d_instance, s3d_type, s3d_group, icon_instance,
         icon_data, icon_width, icon_height, occupant_groups, created_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare SaveLotConfig statement: {}", sqlite3_errmsg(db));
        return false;
    }

    int64_t timestamp = static_cast<int64_t>(std::time(nullptr));

    sqlite3_bind_int(stmt, 1, static_cast<int>(entry.id));
    sqlite3_bind_text(stmt, 2, entry.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, entry.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, static_cast<int>(entry.sizeX));
    sqlite3_bind_int(stmt, 5, static_cast<int>(entry.sizeZ));
    sqlite3_bind_int(stmt, 6, static_cast<int>(entry.buildingExemplarID));
    sqlite3_bind_int(stmt, 7, static_cast<int>(entry.s3dInstance));
    sqlite3_bind_int(stmt, 8, static_cast<int>(entry.s3dType));
    sqlite3_bind_int(stmt, 9, static_cast<int>(entry.s3dGroup));
    sqlite3_bind_int(stmt, 10, static_cast<int>(entry.iconInstance));

    // Bind icon data if provided
    if (!iconRGBA.empty() && entry.iconWidth > 0 && entry.iconHeight > 0) {
        sqlite3_bind_blob(stmt, 11, iconRGBA.data(), static_cast<int>(iconRGBA.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 12, entry.iconWidth);
        sqlite3_bind_int(stmt, 13, entry.iconHeight);
    } else {
        sqlite3_bind_null(stmt, 11);
        sqlite3_bind_null(stmt, 12);
        sqlite3_bind_null(stmt, 13);
    }

    sqlite3_bind_text(stmt, 14, occupantGroupsStr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 15, timestamp);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        LOG_ERROR("Failed to save lot config 0x{:08X}: {}", entry.id, sqlite3_errmsg(db));
        return false;
    }

    LOG_DEBUG("Saved lot config 0x{:08X} ({})", entry.id, entry.name);
    return true;
}

bool PersistentCache::HasLotConfig(uint32_t lotID) {
    if (!db) return false;

    const char* sql = "SELECT 1 FROM lot_configs WHERE lot_id = ? LIMIT 1";
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare HasLotConfig query: {}", sqlite3_errmsg(db));
        return false;
    }

    sqlite3_bind_int(stmt, 1, static_cast<int>(lotID));
    bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);

    return exists;
}

bool PersistentCache::LoadLotConfigMetadata(uint32_t lotID, LotConfigEntry& outEntry) {
    if (!db) return false;

    const char* sql = R"(
        SELECT name, description, size_x, size_z, building_exemplar_id,
               s3d_instance, s3d_type, s3d_group, icon_instance,
               icon_width, icon_height, occupant_groups
        FROM lot_configs
        WHERE lot_id = ?
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare LoadLotConfigMetadata query: {}", sqlite3_errmsg(db));
        return false;
    }

    sqlite3_bind_int(stmt, 1, static_cast<int>(lotID));

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        outEntry.id = lotID;
        outEntry.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        outEntry.description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        outEntry.sizeX = static_cast<uint32_t>(sqlite3_column_int(stmt, 2));
        outEntry.sizeZ = static_cast<uint32_t>(sqlite3_column_int(stmt, 3));
        outEntry.buildingExemplarID = static_cast<uint32_t>(sqlite3_column_int(stmt, 4));
        outEntry.s3dInstance = static_cast<uint32_t>(sqlite3_column_int(stmt, 5));
        outEntry.s3dType = static_cast<uint32_t>(sqlite3_column_int(stmt, 6));
        outEntry.s3dGroup = static_cast<uint32_t>(sqlite3_column_int(stmt, 7));
        outEntry.iconInstance = static_cast<uint32_t>(sqlite3_column_int(stmt, 8));
        outEntry.iconWidth = sqlite3_column_int(stmt, 9);
        outEntry.iconHeight = sqlite3_column_int(stmt, 10);

        // Parse occupant groups from comma-separated string
        const char* occupantGroupsStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));
        if (occupantGroupsStr && occupantGroupsStr[0] != '\0') {
            std::istringstream iss(occupantGroupsStr);
            std::string token;
            while (std::getline(iss, token, ',')) {
                if (!token.empty()) {
                    uint32_t groupID = static_cast<uint32_t>(std::stoul(token, nullptr, 16));
                    outEntry.occupantGroups.insert(groupID);
                }
            }
        }

        // Icon type will be determined by caller based on whether icon is loaded
        outEntry.iconType = LotConfigEntry::IconType::None;
        outEntry.iconSRV = nullptr;

        sqlite3_finalize(stmt);
        return true;
    }

    sqlite3_finalize(stmt);
    return false;
}

bool PersistentCache::LoadLotIconToGPU(
    uint32_t lotID,
    ID3D11Device* pDevice,
    ID3D11ShaderResourceView** outSRV,
    int& outWidth,
    int& outHeight) {

    if (!db || !pDevice || !outSRV) return false;

    const char* sql = "SELECT icon_data, icon_width, icon_height FROM lot_configs WHERE lot_id = ?";
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare LoadLotIconToGPU query: {}", sqlite3_errmsg(db));
        return false;
    }

    sqlite3_bind_int(stmt, 1, static_cast<int>(lotID));

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        // Check if icon data exists
        if (sqlite3_column_type(stmt, 0) == SQLITE_NULL) {
            sqlite3_finalize(stmt);
            return false;
        }

        const void* blobData = sqlite3_column_blob(stmt, 0);
        int blobSize = sqlite3_column_bytes(stmt, 0);
        outWidth = sqlite3_column_int(stmt, 1);
        outHeight = sqlite3_column_int(stmt, 2);

        if (blobData && blobSize > 0) {
            bool success = UploadRGBAToGPU(
                pDevice,
                static_cast<const uint8_t*>(blobData),
                outWidth,
                outHeight,
                outSRV
            );
            sqlite3_finalize(stmt);
            return success;
        }
    }

    sqlite3_finalize(stmt);
    return false;
}

int PersistentCache::GetLotConfigCount() {
    if (!db) return 0;

    const char* sql = "SELECT COUNT(*) FROM lot_configs";
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare GetLotConfigCount query: {}", sqlite3_errmsg(db));
        return 0;
    }

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return count;
}

bool PersistentCache::GetAllLotConfigIDs(std::vector<uint32_t>& outLotIDs) {
    if (!db) return false;

    const char* sql = "SELECT lot_id FROM lot_configs ORDER BY lot_id";
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare GetAllLotConfigIDs query: {}", sqlite3_errmsg(db));
        return false;
    }

    outLotIDs.clear();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        uint32_t lotID = static_cast<uint32_t>(sqlite3_column_int(stmt, 0));
        outLotIDs.push_back(lotID);
    }

    sqlite3_finalize(stmt);
    return true;
}
