#include "CacheDatabase.h"
#include "lots/LotConfigEntry.h"
#include "props/PropCacheEntry.h"
#include "utils/Logger.h"
#include <sqlite3.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Schema version for invalidation
constexpr int CACHE_SCHEMA_VERSION = 1;

CacheDatabase::CacheDatabase() = default;

CacheDatabase::~CacheDatabase() {
    Close();
}

bool CacheDatabase::OpenOrCreate(const std::filesystem::path& dbPath) {
    if (db) {
        Logger::LOG_WARN("Database already open, closing previous connection");
        Close();
    }

    // Open database (creates if doesn't exist)
    int rc = sqlite3_open(dbPath.string().c_str(), &db);
    if (rc != SQLITE_OK) {
        Logger::LOG_ERROR("Failed to open database: {}", sqlite3_errmsg(db));
        sqlite3_close(db);
        db = nullptr;
        return false;
    }

    // Enable WAL mode for better concurrency
    ExecuteSQL("PRAGMA journal_mode=WAL");
    ExecuteSQL("PRAGMA synchronous=NORMAL");

    // Check if schema exists
    if (!ValidateSchema()) {
        // Schema invalid or missing, initialize fresh
        if (!InitializeSchema()) {
            Logger::LOG_ERROR("Failed to initialize database schema");
            Close();
            return false;
        }
    }

    Logger::LOG_INFO("Cache database opened: {}", dbPath.string());
    return true;
}

void CacheDatabase::Close() {
    if (db) {
        sqlite3_close(db);
        db = nullptr;
    }
}

bool CacheDatabase::ValidateSchema() {
    if (!db) return false;

    // Check user_version matches our schema version
    sqlite3_stmt* stmt = PrepareStatement("PRAGMA user_version");
    if (!stmt) return false;

    int version = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        version = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    if (version != CACHE_SCHEMA_VERSION) {
        Logger::LOG_WARN("Schema version mismatch: expected {}, got {}", CACHE_SCHEMA_VERSION, version);
        return false;
    }

    // Check required tables exist
    const char* checkTables[] = {"lots", "props", "cache_meta"};
    for (const char* table : checkTables) {
        std::string sql = "SELECT name FROM sqlite_master WHERE type='table' AND name='" + std::string(table) + "'";
        stmt = PrepareStatement(sql.c_str());
        if (!stmt) return false;

        bool found = (sqlite3_step(stmt) == SQLITE_ROW);
        sqlite3_finalize(stmt);

        if (!found) {
            Logger::LOG_WARN("Required table missing: {}", table);
            return false;
        }
    }

    return true;
}

bool CacheDatabase::InitializeSchema() {
    if (!db) return false;

    Logger::LOG_INFO("Initializing cache database schema v{}", CACHE_SCHEMA_VERSION);

    // Drop existing tables if any
    ExecuteSQL("DROP TABLE IF EXISTS lots");
    ExecuteSQL("DROP TABLE IF EXISTS props");
    ExecuteSQL("DROP TABLE IF EXISTS cache_meta");

    // Create lots table (composite key: exemplar_group + exemplar_instance for uniqueness)
    const char* createLots = R"(
        CREATE TABLE lots (
            exemplar_group INTEGER NOT NULL,
            exemplar_instance INTEGER NOT NULL,
            name TEXT NOT NULL,
            description TEXT,
            size_x INTEGER,
            size_z INTEGER,
            min_capacity INTEGER,
            max_capacity INTEGER,
            growth_stage INTEGER,
            icon_type INTEGER,
            icon_width INTEGER,
            icon_height INTEGER,
            occupant_groups TEXT,
            thumbnail_blob BLOB,
            PRIMARY KEY (exemplar_group, exemplar_instance)
        )
    )";

    if (!ExecuteSQL(createLots)) {
        Logger::LOG_ERROR("Failed to create lots table");
        return false;
    }

    // Create props table (composite key: exemplar_group + exemplar_instance for uniqueness)
    const char* createProps = R"(
        CREATE TABLE props (
            exemplar_group INTEGER NOT NULL,
            exemplar_instance INTEGER NOT NULL,
            name TEXT,
            s3d_group INTEGER,
            s3d_instance INTEGER,
            icon_type INTEGER,
            icon_width INTEGER,
            icon_height INTEGER,
            family_type INTEGER,
            thumbnail_blob BLOB,
            PRIMARY KEY (exemplar_group, exemplar_instance)
        )
    )";

    if (!ExecuteSQL(createProps)) {
        Logger::LOG_ERROR("Failed to create props table");
        return false;
    }

    // Create cache_meta table
    const char* createMeta = R"(
        CREATE TABLE cache_meta (
            key TEXT PRIMARY KEY,
            value TEXT
        )
    )";

    if (!ExecuteSQL(createMeta)) {
        Logger::LOG_ERROR("Failed to create cache_meta table");
        return false;
    }

    // Note: Composite primary keys are automatically indexed, no additional indices needed

    // Set schema version
    char versionSQL[64];
    snprintf(versionSQL, sizeof(versionSQL), "PRAGMA user_version = %d", CACHE_SCHEMA_VERSION);
    if (!ExecuteSQL(versionSQL)) {
        Logger::LOG_ERROR("Failed to set schema version");
        return false;
    }

    return true;
}

sqlite3_stmt* CacheDatabase::PrepareStatement(const char* sql) {
    if (!db) return nullptr;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::LOG_ERROR("Failed to prepare statement: {}", sqlite3_errmsg(db));
        return nullptr;
    }
    return stmt;
}

bool CacheDatabase::ExecuteSQL(const char* sql) {
    if (!db) return false;

    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        Logger::LOG_ERROR("SQL execution failed: {}", errMsg ? errMsg : "unknown error");
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

// ==================== Metadata Operations ====================

bool CacheDatabase::SetMetadata(const std::string& key, const std::string& value) {
    if (!db) return false;

    const char* sql = "INSERT OR REPLACE INTO cache_meta (key, value) VALUES (?, ?)";
    sqlite3_stmt* stmt = PrepareStatement(sql);
    if (!stmt) return false;

    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_TRANSIENT);

    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

std::string CacheDatabase::GetMetadata(const std::string& key) {
    if (!db) return "";

    const char* sql = "SELECT value FROM cache_meta WHERE key = ?";
    sqlite3_stmt* stmt = PrepareStatement(sql);
    if (!stmt) return "";

    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);

    std::string value;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (text) value = text;
    }

    sqlite3_finalize(stmt);
    return value;
}

// ==================== Lot Operations ====================

bool CacheDatabase::SaveLot(const LotConfigEntry& lot, const std::vector<uint8_t>& ddsThumbnail) {
    if (!db) return false;

    const char* sql = R"(
        INSERT OR REPLACE INTO lots (
            exemplar_group, exemplar_instance, name, description,
            size_x, size_z, min_capacity, max_capacity, growth_stage,
            icon_type, icon_width, icon_height, occupant_groups, thumbnail_blob
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt = PrepareStatement(sql);
    if (!stmt) return false;

    // Serialize occupant groups to JSON array
    json occupantGroupsJson = json::array();
    for (uint32_t group : lot.occupantGroups) {
        occupantGroupsJson.push_back(group);
    }
    std::string occupantGroupsStr = occupantGroupsJson.dump();

    // Bind parameters (id IS the exemplar instance)
    sqlite3_bind_int(stmt, 1, lot.id);
    sqlite3_bind_int(stmt, 2, lot.exemplarGroup);
    sqlite3_bind_text(stmt, 3, lot.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, lot.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, lot.sizeX);
    sqlite3_bind_int(stmt, 6, lot.sizeZ);
    sqlite3_bind_int(stmt, 7, lot.minCapacity);
    sqlite3_bind_int(stmt, 8, lot.maxCapacity);
    sqlite3_bind_int(stmt, 9, lot.growthStage);
    sqlite3_bind_int(stmt, 10, static_cast<int>(lot.iconType));
    sqlite3_bind_int(stmt, 11, lot.iconWidth);
    sqlite3_bind_int(stmt, 12, lot.iconHeight);
    sqlite3_bind_text(stmt, 13, occupantGroupsStr.c_str(), -1, SQLITE_TRANSIENT);

    // Bind thumbnail BLOB
    if (!ddsThumbnail.empty()) {
        sqlite3_bind_blob(stmt, 14, ddsThumbnail.data(), static_cast<int>(ddsThumbnail.size()), SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 14);
    }

    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

std::optional<std::pair<LotConfigEntry, std::vector<uint8_t>>> CacheDatabase::LoadLot(uint32_t exemplarGroup, uint32_t exemplarInstance) {
    if (!db) return std::nullopt;

    const char* sql = R"(
        SELECT exemplar_group, exemplar_instance, name, description,
               size_x, size_z, min_capacity, max_capacity, growth_stage,
               icon_type, icon_width, icon_height, occupant_groups, thumbnail_blob
        FROM lots WHERE exemplar_group = ? AND exemplar_instance = ?
    )";

    sqlite3_stmt* stmt = PrepareStatement(sql);
    if (!stmt) return std::nullopt;

    sqlite3_bind_int(stmt, 1, exemplarGroup);
    sqlite3_bind_int(stmt, 2, exemplarInstance);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return std::nullopt;
    }

    // Load entry
    LotConfigEntry lot;
    lot.exemplarGroup = sqlite3_column_int(stmt, 0);
    lot.id = sqlite3_column_int(stmt, 1); // id IS the exemplar instance

    const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    if (name) lot.name = name;

    const char* desc = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    if (desc) lot.description = desc;

    lot.sizeX = sqlite3_column_int(stmt, 4);
    lot.sizeZ = sqlite3_column_int(stmt, 5);
    lot.minCapacity = sqlite3_column_int(stmt, 6);
    lot.maxCapacity = sqlite3_column_int(stmt, 7);
    lot.growthStage = sqlite3_column_int(stmt, 8);
    lot.iconType = static_cast<LotConfigEntry::IconType>(sqlite3_column_int(stmt, 9));
    lot.iconWidth = sqlite3_column_int(stmt, 10);
    lot.iconHeight = sqlite3_column_int(stmt, 11);

    // Deserialize occupant groups from JSON
    const char* occupantGroupsStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 12));
    if (occupantGroupsStr) {
        try {
            json occupantGroupsJson = json::parse(occupantGroupsStr);
            for (const auto& group : occupantGroupsJson) {
                lot.occupantGroups.insert(group.get<uint32_t>());
            }
        } catch (const json::exception& e) {
            Logger::LOG_WARN("Failed to parse occupant groups for lot {}: {}", lot.id, e.what());
        }
    }

    // Load thumbnail BLOB
    std::vector<uint8_t> thumbnail;
    const void* blobData = sqlite3_column_blob(stmt, 13);
    int blobSize = sqlite3_column_bytes(stmt, 13);
    if (blobData && blobSize > 0) {
        thumbnail.resize(blobSize);
        std::memcpy(thumbnail.data(), blobData, blobSize);
    }

    sqlite3_finalize(stmt);
    return std::make_pair(std::move(lot), std::move(thumbnail));
}

std::vector<std::pair<uint32_t, uint32_t>> CacheDatabase::GetAllLotKeys() {
    std::vector<std::pair<uint32_t, uint32_t>> keys;
    if (!db) return keys;

    const char* sql = "SELECT exemplar_group, exemplar_instance FROM lots";
    sqlite3_stmt* stmt = PrepareStatement(sql);
    if (!stmt) return keys;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        uint32_t group = sqlite3_column_int(stmt, 0);
        uint32_t instance = sqlite3_column_int(stmt, 1);
        keys.emplace_back(group, instance);
    }

    sqlite3_finalize(stmt);
    return keys;
}

// ==================== Prop Operations ====================

bool CacheDatabase::SaveProp(const PropCacheEntry& prop, const std::vector<uint8_t>& ddsThumbnail) {
    if (!db) return false;

    const char* sql = R"(
        INSERT OR REPLACE INTO props (
            exemplar_group, exemplar_instance, name,
            s3d_group, s3d_instance, icon_type, icon_width, icon_height,
            family_type, thumbnail_blob
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt = PrepareStatement(sql);
    if (!stmt) return false;

    // Bind parameters (exemplarIID IS the exemplar instance)
    sqlite3_bind_int(stmt, 1, prop.exemplarGroup);
    sqlite3_bind_int(stmt, 2, prop.exemplarIID);
    sqlite3_bind_text(stmt, 3, prop.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, prop.s3dGroup);
    sqlite3_bind_int(stmt, 5, prop.s3dInstance);
    sqlite3_bind_int(stmt, 6, static_cast<int>(prop.iconType));
    sqlite3_bind_int(stmt, 7, prop.iconWidth);
    sqlite3_bind_int(stmt, 8, prop.iconHeight);
    sqlite3_bind_int(stmt, 9, prop.familyType);

    // Bind thumbnail BLOB
    if (!ddsThumbnail.empty()) {
        sqlite3_bind_blob(stmt, 10, ddsThumbnail.data(), static_cast<int>(ddsThumbnail.size()), SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 10);
    }

    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

std::optional<std::pair<PropCacheEntry, std::vector<uint8_t>>> CacheDatabase::LoadProp(uint32_t exemplarGroup, uint32_t exemplarInstance) {
    if (!db) return std::nullopt;

    const char* sql = R"(
        SELECT exemplar_group, exemplar_instance, name,
               s3d_group, s3d_instance, icon_type, icon_width, icon_height,
               family_type, thumbnail_blob
        FROM props WHERE exemplar_group = ? AND exemplar_instance = ?
    )";

    sqlite3_stmt* stmt = PrepareStatement(sql);
    if (!stmt) return std::nullopt;

    sqlite3_bind_int(stmt, 1, exemplarGroup);
    sqlite3_bind_int(stmt, 2, exemplarInstance);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return std::nullopt;
    }

    // Load entry
    PropCacheEntry prop;
    prop.exemplarGroup = sqlite3_column_int(stmt, 0);
    prop.exemplarIID = sqlite3_column_int(stmt, 1);
    prop.propID = prop.exemplarIID; // Same as exemplarIID

    const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    if (name) prop.name = name;

    prop.s3dGroup = sqlite3_column_int(stmt, 3);
    prop.s3dInstance = sqlite3_column_int(stmt, 4);
    prop.iconType = static_cast<PropCacheEntry::IconType>(sqlite3_column_int(stmt, 5));
    prop.iconWidth = sqlite3_column_int(stmt, 6);
    prop.iconHeight = sqlite3_column_int(stmt, 7);
    prop.familyType = sqlite3_column_int(stmt, 8);

    // Load thumbnail BLOB
    std::vector<uint8_t> thumbnail;
    const void* blobData = sqlite3_column_blob(stmt, 9);
    int blobSize = sqlite3_column_bytes(stmt, 9);
    if (blobData && blobSize > 0) {
        thumbnail.resize(blobSize);
        std::memcpy(thumbnail.data(), blobData, blobSize);
    }

    sqlite3_finalize(stmt);
    return std::make_pair(std::move(prop), std::move(thumbnail));
}

std::vector<std::pair<uint32_t, uint32_t>> CacheDatabase::GetAllPropKeys() {
    std::vector<std::pair<uint32_t, uint32_t>> keys;
    if (!db) return keys;

    const char* sql = "SELECT exemplar_group, exemplar_instance FROM props";
    sqlite3_stmt* stmt = PrepareStatement(sql);
    if (!stmt) return keys;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        uint32_t group = sqlite3_column_int(stmt, 0);
        uint32_t instance = sqlite3_column_int(stmt, 1);
        keys.emplace_back(group, instance);
    }

    sqlite3_finalize(stmt);
    return keys;
}

// ==================== Bulk Operations ====================

bool CacheDatabase::BeginTransaction() {
    return ExecuteSQL("BEGIN TRANSACTION");
}

bool CacheDatabase::CommitTransaction() {
    return ExecuteSQL("COMMIT");
}

void CacheDatabase::RollbackTransaction() {
    ExecuteSQL("ROLLBACK");
}
