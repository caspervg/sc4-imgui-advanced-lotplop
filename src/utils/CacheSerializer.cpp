#include "CacheSerializer.h"
#include "../ui/LotConfigEntry.h"
#include "../utils/Logger.h"
#include "mini/ini.h"
#include <sstream>

namespace CacheSerializer {

static std::string JoinUint32CSV(const std::unordered_set<uint32_t>& set) {
    std::ostringstream oss;
    bool first = true;
    for (auto v : set) {
        if (!first) oss << ","; else first = false;
        oss << v; // decimal for simplicity/readability
    }
    return oss.str();
}

static void ParseUint32CSV(const std::string& csv, std::unordered_set<uint32_t>& out) {
    out.clear();
    std::istringstream iss(csv);
    std::string token;
    while (std::getline(iss, token, ',')) {
        if (token.empty()) continue;
        try {
            // allow 0x... hex as well
            uint32_t val = 0;
            if (token.size() > 2 && (token[0] == '0') && (token[1] == 'x' || token[1] == 'X')) {
                val = static_cast<uint32_t>(std::stoul(token, nullptr, 16));
            } else {
                val = static_cast<uint32_t>(std::stoul(token, nullptr, 10));
            }
            out.insert(val);
        } catch (...) {
            // ignore malformed tokens
        }
    }
}

bool SaveLotCacheINI(const std::unordered_map<uint32_t, LotConfigEntry>& cache,
                     const std::string& filename,
                     const std::string& pluginVersion,
                     int schemaVersion) {
    try {
        mINI::INIStructure ini;

        // Meta section
        ini["Meta"]["version"] = std::to_string(schemaVersion);
        ini["Meta"]["plugin_version"] = pluginVersion;
        ini["Meta"]["count"] = std::to_string(cache.size());

        // One section per lot
        for (const auto& kv : cache) {
            const auto& e = kv.second;
            std::string section = "Lot:" + std::to_string(e.id);
            auto& sec = ini[section];
            sec["id"] = std::to_string(e.id);
            sec["name"] = e.name;
            sec["description"] = e.description;
            sec["sizeX"] = std::to_string(e.sizeX);
            sec["sizeZ"] = std::to_string(e.sizeZ);
            sec["minCapacity"] = std::to_string(e.minCapacity);
            sec["maxCapacity"] = std::to_string(e.maxCapacity);
            sec["growthStage"] = std::to_string(static_cast<uint32_t>(e.growthStage));
            sec["iconInstance"] = std::to_string(e.iconInstance);
            sec["occupantGroups"] = JoinUint32CSV(e.occupantGroups);
        }

        mINI::INIFile file(filename);
        if (!file.generate(ini, true)) { // true to pretty-print
            LOG_WARN("Failed to write INI cache: {}", filename);
            return false;
        }
        LOG_INFO("Saved lot cache (INI) to {} ({} entries)", filename, cache.size());
        return true;
    } catch (...) {
        LOG_WARN("Exception while saving INI cache: {}", filename);
        return false;
    }
}

bool LoadLotCacheINI(std::unordered_map<uint32_t, LotConfigEntry>& outCache,
                     const std::string& filename,
                     int expectedSchemaVersion) {
    try {
        mINI::INIStructure ini;
        mINI::INIFile file(filename);
        if (!file.read(ini)) {
            LOG_INFO("No INI cache found at {}", filename);
            return false;
        }

        int version = 0;
        if (ini.has("Meta")) {
            auto v = ini["Meta"]["version"]; if (!v.empty()) version = std::stoi(v);
        }
        if (version != expectedSchemaVersion) {
            LOG_INFO("INI cache version mismatch ({} != {}), ignoring {}", version, expectedSchemaVersion, filename);
            return false;
        }

        outCache.clear();
        for (const auto& pair : ini) {
            const std::string& section = pair.first;
            if (section.rfind("lot:", 0) != 0) continue; // skip non-lot sections
            const auto& sec = pair.second;
            LotConfigEntry e{};
            // id: derive from section if missing
            std::string idStr = sec.get("id");
            if (idStr.empty()) {
                idStr = section.substr(4); // after "Lot:"
            }
            try { e.id = static_cast<uint32_t>(std::stoul(idStr)); }
            catch (...) { continue; }

            e.name = sec.get("name");
            e.description = sec.get("description");
            {
                auto s = sec.get("sizeX"); if (!s.empty()) e.sizeX = static_cast<uint32_t>(std::stoul(s));
            }
            {
                auto s = sec.get("sizeZ"); if (!s.empty()) e.sizeZ = static_cast<uint32_t>(std::stoul(s));
            }
            {
                auto s = sec.get("minCapacity"); if (!s.empty()) e.minCapacity = static_cast<uint16_t>(std::stoul(s));
            }
            {
                auto s = sec.get("maxCapacity"); if (!s.empty()) e.maxCapacity = static_cast<uint16_t>(std::stoul(s));
            }
            {
                auto s = sec.get("growthStage"); if (!s.empty()) e.growthStage = static_cast<uint8_t>(std::stoul(s));
            }
            {
                auto s = sec.get("iconInstance"); if (!s.empty()) e.iconInstance = static_cast<uint32_t>(std::stoul(s));
            }
            ParseUint32CSV(sec.get("occupantGroups"), e.occupantGroups);

            outCache[e.id] = std::move(e);
        }
        return !outCache.empty();
    } catch (...) {
        LOG_WARN("Exception while loading INI cache: {}", filename);
        return false;
    }
}

} // namespace CacheSerializer
