#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Forward declarations
struct LotConfigEntry;
struct PropCacheEntry;
struct sqlite3;

/**
 * @brief SQLite-based persistent cache for lot and prop data with PNG thumbnails.
 *
 * Provides fast loading/saving of cache entries to avoid expensive exemplar scanning
 * and S3D thumbnail generation on every game launch.
 *
 * Schema Version: 1
 * - lots table: LotConfigEntry metadata + PNG thumbnail BLOB
 * - props table: PropCacheEntry metadata + PNG thumbnail BLOB
 * - cache_meta table: Extensible key-value metadata (version, timestamps, etc.)
 */
class CacheDatabase {
public:
    CacheDatabase();
    ~CacheDatabase();

    // Disable copy (sqlite3* is not copyable)
    CacheDatabase(const CacheDatabase&) = delete;
    CacheDatabase& operator=(const CacheDatabase&) = delete;

    /**
     * @brief Opens existing database or creates new one with schema.
     * @param dbPath Path to .sqlite file (e.g., "AdvancedLotPlopCache.sqlite")
     * @return true if successful, false on error
     */
    bool OpenOrCreate(const std::filesystem::path& dbPath);

    /**
     * @brief Closes database connection and commits pending changes.
     */
    void Close();

    /**
     * @brief Checks if database is open and valid.
     */
    bool IsValid() const { return db != nullptr; }

    // ==================== Metadata Operations ====================

    /**
     * @brief Sets a metadata key-value pair (for versioning, timestamps, etc.)
     * @param key Metadata key (e.g., "cache_version", "last_rebuild")
     * @param value String value
     * @return true if successful
     */
    bool SetMetadata(const std::string& key, const std::string& value);

    /**
     * @brief Gets a metadata value by key.
     * @return Value string, or empty string if key not found
     */
    std::string GetMetadata(const std::string& key);

    // ==================== Lot Operations ====================

    /**
     * @brief Saves a lot entry with its PNG thumbnail to the database.
     * @param lot Lot configuration entry
     * @param pngThumbnail PNG-encoded thumbnail binary data (can be empty)
     * @return true if successful
     */
    bool SaveLot(const LotConfigEntry& lot, const std::vector<uint8_t>& pngThumbnail);

    /**
     * @brief Loads a lot entry by exemplar key.
     * @param exemplarGroup Exemplar group ID
     * @param exemplarInstance Exemplar instance ID (lot ID)
     * @return Pair of (LotConfigEntry, PNG thumbnail bytes), or nullopt if not found
     */
    std::optional<std::pair<LotConfigEntry, std::vector<uint8_t>>> LoadLot(uint32_t exemplarGroup, uint32_t exemplarInstance);

    /**
     * @brief Gets all lot exemplar keys in the database.
     * @return Vector of (exemplar_group, exemplar_instance) pairs
     */
    std::vector<std::pair<uint32_t, uint32_t>> GetAllLotKeys();

    // ==================== Prop Operations ====================

    /**
     * @brief Saves a prop entry with its PNG thumbnail to the database.
     * @param prop Prop cache entry
     * @param pngThumbnail PNG-encoded thumbnail binary data (can be empty)
     * @return true if successful
     */
    bool SaveProp(const PropCacheEntry& prop, const std::vector<uint8_t>& pngThumbnail);

    /**
     * @brief Loads a prop entry by exemplar key.
     * @param exemplarGroup Exemplar group ID
     * @param exemplarInstance Exemplar instance ID (same as prop_id)
     * @return Pair of (PropCacheEntry, PNG thumbnail bytes), or nullopt if not found
     */
    std::optional<std::pair<PropCacheEntry, std::vector<uint8_t>>> LoadProp(uint32_t exemplarGroup, uint32_t exemplarInstance);

    /**
     * @brief Gets all prop exemplar keys in the database.
     * @return Vector of (exemplar_group, exemplar_instance) pairs
     */
    std::vector<std::pair<uint32_t, uint32_t>> GetAllPropKeys();

    // ==================== Bulk Operations ====================

    /**
     * @brief Begins a transaction for bulk operations (much faster).
     * Call CommitTransaction() when done or RollbackTransaction() on error.
     * @return true if successful
     */
    bool BeginTransaction();

    /**
     * @brief Commits the current transaction.
     * @return true if successful
     */
    bool CommitTransaction();

    /**
     * @brief Rolls back the current transaction (on error).
     */
    void RollbackTransaction();

private:
    sqlite3* db = nullptr;

    /**
     * @brief Validates that the database schema matches expected version.
     * @return true if schema is valid
     */
    bool ValidateSchema();

    /**
     * @brief Creates the initial database schema (tables, indices).
     * @return true if successful
     */
    bool InitializeSchema();

    /**
     * @brief Prepares SQL statement and logs on error.
     * @return sqlite3_stmt* or nullptr on error
     */
    struct sqlite3_stmt* PrepareStatement(const char* sql);

    /**
     * @brief Executes a simple SQL statement (no result rows).
     * @return true if successful
     */
    bool ExecuteSQL(const char* sql);
};
