#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Config {
    // Loads configuration from SC4AdvancedLotPlop.ini if present.
    // Currently supports [OccupantGroups] section mapping hex IDs to display names.
    void LoadOnce();

    // Map of occupant group ID -> display name.
    const std::unordered_map<uint32_t, std::string>& GetOccupantGroupNames();

    std::string GetModuleDir();

    // Persisted UI state loaded from / saved to SC4AdvancedLotPlop.ini
    struct UIState {
        uint8_t zoneFilter = 0xFF;
        uint8_t wealthFilter = 0xFF;
        uint32_t minSizeX = 1, maxSizeX = 16;
        uint32_t minSizeZ = 1, maxSizeZ = 16;
        std::string search; // raw search buffer
        std::vector<uint32_t> selectedGroups; // occupant groups selected in filter
        uint32_t selectedLotID = 0; // last selected lot
        std::vector<uint32_t> favorites; // persisted favorite lot IDs
        bool favoritesOnly = false; // show only favorites filter
    };

    // Returns reference to loaded UI state (LoadOnce ensures initialization)
    const UIState& GetUIState();

    // Saves given UI state back to INI (overwrites [UI] section keys)
    void SaveUIState(const UIState& state);
}
