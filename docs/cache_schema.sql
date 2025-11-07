-- ============================================================================
-- SC4 Advanced Lot Plop - Cache Database Schema
-- ============================================================================
-- Version: 1
-- Purpose: Persistent storage for lot/prop metadata and PNG thumbnails
-- Format: SQLite 3
-- File: AdvancedLotPlopCache.sqlite
-- ============================================================================

-- Schema version control
PRAGMA user_version = 1;

-- Performance settings
PRAGMA journal_mode = WAL;
PRAGMA synchronous = NORMAL;

-- ============================================================================
-- Lots Table
-- ============================================================================
-- Stores cached lot configuration entries with metadata and thumbnails.
-- Composite primary key (exemplar_group, exemplar_instance) ensures uniqueness.
-- ============================================================================

CREATE TABLE lots (
    -- Exemplar resource key (composite primary key)
    exemplar_group INTEGER NOT NULL,        -- Exemplar group ID (e.g., 0x00000000, 0x...)
    exemplar_instance INTEGER NOT NULL,     -- Exemplar instance ID (same as lot.id)

    -- Lot metadata
    name TEXT NOT NULL,                     -- Lot name (localized if available)
    description TEXT,                       -- Lot description (localized)

    -- Dimensions
    size_x INTEGER,                         -- Lot width in tiles
    size_z INTEGER,                         -- Lot depth in tiles

    -- Capacity
    min_capacity INTEGER,                   -- Minimum capacity
    max_capacity INTEGER,                   -- Maximum capacity
    growth_stage INTEGER,                   -- Growth stage (0-4)

    -- Icon metadata
    icon_type INTEGER,                      -- IconType enum: 0=None, 1=PNG, 2=S3D
    icon_width INTEGER,                     -- Icon/thumbnail width in pixels
    icon_height INTEGER,                    -- Icon/thumbnail height in pixels

    -- Classification
    occupant_groups TEXT,                   -- JSON array of occupant group IDs (e.g., "[0x00001500, 0x00001501]")

    -- Thumbnail data
    thumbnail_blob BLOB,                    -- PNG-encoded thumbnail (~2-3 KB for 44x44)

    PRIMARY KEY (exemplar_group, exemplar_instance)
);

-- ============================================================================
-- Props Table
-- ============================================================================
-- Stores cached prop entries with metadata and thumbnails.
-- Composite primary key (exemplar_group, exemplar_instance) ensures uniqueness.
-- ============================================================================

CREATE TABLE props (
    -- Exemplar resource key (composite primary key)
    exemplar_group INTEGER NOT NULL,        -- Exemplar group ID
    exemplar_instance INTEGER NOT NULL,     -- Exemplar instance ID (same as propID/exemplarIID)

    -- Prop metadata
    name TEXT,                              -- Prop name

    -- S3D model resource key (from RKT property)
    s3d_group INTEGER,                      -- S3D model group ID
    s3d_instance INTEGER,                   -- S3D model instance ID

    -- Icon metadata
    icon_type INTEGER,                      -- IconType enum: 0=None, 1=PNG, 2=S3D
    icon_width INTEGER,                     -- Thumbnail width in pixels
    icon_height INTEGER,                    -- Thumbnail height in pixels

    -- Classification
    family_type INTEGER,                    -- Prop family type (if applicable)

    -- Thumbnail data
    thumbnail_blob BLOB,                    -- PNG-encoded thumbnail (~2-3 KB for 44x44)

    PRIMARY KEY (exemplar_group, exemplar_instance)
);

-- ============================================================================
-- Cache Metadata Table
-- ============================================================================
-- Extensible key-value store for cache versioning, timestamps, and metadata.
-- ============================================================================

CREATE TABLE cache_meta (
    key TEXT PRIMARY KEY,                   -- Metadata key
    value TEXT                              -- Metadata value (string)
);

-- Example metadata entries:
-- INSERT INTO cache_meta (key, value) VALUES ('cache_version', '1');
-- INSERT INTO cache_meta (key, value) VALUES ('last_rebuild', '2025-11-07T12:00:00Z');
-- INSERT INTO cache_meta (key, value) VALUES ('game_version', '1.0.0.641');
-- INSERT INTO cache_meta (key, value) VALUES ('schema_version', '1');

-- ============================================================================
-- Notes
-- ============================================================================
-- 1. Composite primary keys (exemplar_group, exemplar_instance) are automatically
--    indexed by SQLite, so no additional indices are needed.
--
-- 2. Thumbnail BLOBs are stored as PNG format for good compression and simplicity:
--    - Typical size: ~2-3 KB for 44x44 thumbnail (3x better than raw RGBA8)
--    - Uses existing WIC infrastructure for encoding/decoding
--    - Standard format - easily inspectable with image viewers
--
-- 3. Occupant groups are stored as JSON arrays for flexibility:
--    - Example: "[2684354816, 2684354817]" or "[0xA0000000, 0xA0000001]"
--    - Can be parsed back to unordered_set<uint32_t> on load
--
-- 4. Cache invalidation strategies:
--    - Check cache_version against expected version
--    - Check last_rebuild timestamp for expiration
--    - Check game_version for SC4 updates
--
-- 5. Example queries:
--    - Get all residential lots: SELECT * FROM lots WHERE occupant_groups LIKE '%0x00001100%'
--    - Count props by family: SELECT family_type, COUNT(*) FROM props GROUP BY family_type
--    - Check cache size: SELECT page_count * page_size as size FROM pragma_page_count(), pragma_page_size();
-- ============================================================================
