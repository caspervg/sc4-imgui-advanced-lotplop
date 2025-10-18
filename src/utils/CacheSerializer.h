#pragma once
#include <string>
#include <unordered_map>
#include <cstdint>

struct LotConfigEntry;

namespace CacheSerializer {
    // Saves lot cache to an INI file using mINI.
    // Returns true on success.
    bool SaveLotCacheINI(const std::unordered_map<uint32_t, LotConfigEntry>& cache,
                         const std::string& filename,
                         const std::string& pluginVersion,
                         int schemaVersion = 1);

    // Loads lot cache from an INI file into the provided map (clearing it first).
    // Returns true on success and when version matches schemaVersion.
    bool LoadLotCacheINI(std::unordered_map<uint32_t, LotConfigEntry>& outCache,
                         const std::string& filename,
                         int expectedSchemaVersion = 1);
}
