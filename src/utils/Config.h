#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

namespace Config {
    // Loads configuration from SC4AdvancedLotPlop.ini if present.
    // Currently supports [OccupantGroups] section mapping hex IDs to display names.
    void LoadOnce();

    // Map of occupant group ID -> display name.
    const std::unordered_map<uint32_t, std::string>& GetOccupantGroupNames();
}
