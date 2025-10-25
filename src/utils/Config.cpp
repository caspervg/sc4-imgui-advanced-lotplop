#include "Config.h"

#include <mutex>
#include <windows.h>
#include <mINI/ini.h>
#include <sstream>
#include <algorithm>

namespace Config {
    static std::once_flag g_once;

    static std::unordered_map<uint32_t, std::string>& GetGroupNamesMap() {
        static std::unordered_map<uint32_t, std::string> g_groupNames{};
        return g_groupNames;
    }

    static UIState& GetMutableUIState() {
        static UIState state{}; // defaults already set
        return state;
    }

    static std::string Trim(const std::string& s) {
        size_t a = s.find_first_not_of(" \t\r\n");
        size_t b = s.find_last_not_of(" \t\r\n");
        if (a == std::string::npos) return std::string();
        return s.substr(a, b - a + 1);
    }

    static std::string GetModuleDir() {
        char path[MAX_PATH] = {0};
        HMODULE hMod = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                (LPCSTR)&GetModuleDir,
                                &hMod)) {
            GetModuleFileNameA(hMod, path, MAX_PATH);
            std::string p = path;
            size_t pos = p.find_last_of("/\\");
            if (pos != std::string::npos) {
                p.resize(pos);
                return p;
            }
        }
        return std::string(".");
    }

    static uint32_t ParseUInt(const std::string& s) {
        std::string t = Trim(s);
        if (t.empty()) return 0;
        if (t.rfind("0x", 0) == 0 || t.rfind("0X", 0) == 0) {
            return static_cast<uint32_t>(strtoul(t.c_str(), nullptr, 16));
        }
        return static_cast<uint32_t>(strtoul(t.c_str(), nullptr, 10));
    }

    static void LoadInternal() {
        // Try to load SC4AdvancedLotPlop.ini next to the DLL
        std::string iniPath = GetModuleDir() + "\\SC4AdvancedLotPlop.ini";

        mINI::INIFile file(iniPath);
        mINI::INIStructure ini;
        if (file.read(ini)) {
            if (ini.has("OccupantGroups")) {
                auto& section = ini["OccupantGroups"];
                for (auto it = section.begin(); it != section.end(); ++it) {
                    // Make copies to avoid potential iterator lifetime issues
                    std::string rawKey = it->first;
                    std::string rawVal = it->second;
                    std::string key = Trim(rawKey);
                    std::string value = Trim(rawVal);
                    if (key.empty()) continue;

                    // Accept formats: 0xHHHHHHHH or decimal
                    uint32_t id = 0;
                    if (key.rfind("0x", 0) == 0 || key.rfind("0X", 0) == 0) {
                        id = static_cast<uint32_t>(strtoul(key.c_str(), nullptr, 16));
                    } else {
                        id = static_cast<uint32_t>(strtoul(key.c_str(), nullptr, 10));
                    }
                    if (id != 0 && !value.empty()) {
                        GetGroupNamesMap()[id] = std::move(value);
                    }
                }
            }
            if (ini.has("UI")) {
                auto& uiSec = ini["UI"];
                UIState& st = GetMutableUIState();
                if (uiSec.has("ZoneFilter")) st.zoneFilter = static_cast<uint8_t>(ParseUInt(uiSec["ZoneFilter"]));
                if (uiSec.has("WealthFilter")) st.wealthFilter = static_cast<uint8_t>(ParseUInt(uiSec["WealthFilter"]));
                if (uiSec.has("MinSizeX")) st.minSizeX = ParseUInt(uiSec["MinSizeX"]);
                if (uiSec.has("MaxSizeX")) st.maxSizeX = ParseUInt(uiSec["MaxSizeX"]);
                if (uiSec.has("MinSizeZ")) st.minSizeZ = ParseUInt(uiSec["MinSizeZ"]);
                if (uiSec.has("MaxSizeZ")) st.maxSizeZ = ParseUInt(uiSec["MaxSizeZ"]);
                if (uiSec.has("Search")) st.search = uiSec["Search"];
                if (uiSec.has("SelectedLot")) st.selectedLotID = ParseUInt(uiSec["SelectedLot"]);
                if (uiSec.has("SelectedGroups")) {
                    st.selectedGroups.clear();
                    std::string csv = uiSec["SelectedGroups"];
                    std::istringstream iss(csv);
                    std::string tok;
                    while (std::getline(iss, tok, ',')) {
                        uint32_t id = ParseUInt(tok);
                        if (id) st.selectedGroups.push_back(id);
                    }
                }
                if (uiSec.has("Favorites")) {
                    st.favorites.clear();
                    std::string csv = uiSec["Favorites"];
                	std::istringstream iss(csv);
                	std::string tok;
                	while (std::getline(iss, tok, ','))
                		{ uint32_t id = ParseUInt(tok); if (id) st.favorites.push_back(id); }
                }
                if (uiSec.has("FavoritesOnly")) st.favoritesOnly = (ParseUInt(uiSec["FavoritesOnly"]) != 0);
            }
        }

        // Provide a few sensible defaults if none loaded
        auto& groupNames = GetGroupNamesMap();
        if (groupNames.empty()) {
            groupNames[0x0000150A] = "Landmark";
            groupNames[0x0000150B] = "Reward";
            groupNames[0x00001920] = "Parks";
            groupNames[0x00001935] = "Education";
            groupNames[0x0000192A] = "Health";
            groupNames[0x0000190E] = "Utilities";
            groupNames[0x00001927] = "Police";
            groupNames[0x00001928] = "Fire";
            groupNames[0x0000192B] = "Transit";
        }
    }

    void LoadOnce() {
        std::call_once(g_once, [](){ LoadInternal(); });
    }

    const std::unordered_map<uint32_t, std::string>& GetOccupantGroupNames() {
        LoadOnce();
        return GetGroupNamesMap();
    }

    const UIState& GetUIState() {
        LoadOnce();
        return GetMutableUIState();
    }

    void SaveUIState(const UIState& state) {
        LoadOnce(); // ensure ini path resolution
        std::string iniPath = GetModuleDir() + "\\SC4AdvancedLotPlop.ini";
        mINI::INIFile file(iniPath);
        mINI::INIStructure ini;
        // Read existing to preserve other sections
        file.read(ini);

        auto& uiSec = ini["UI"]; // creates if missing
        uiSec["ZoneFilter"] = std::to_string(state.zoneFilter);
        uiSec["WealthFilter"] = std::to_string(state.wealthFilter);
        uiSec["MinSizeX"] = std::to_string(state.minSizeX);
        uiSec["MaxSizeX"] = std::to_string(state.maxSizeX);
        uiSec["MinSizeZ"] = std::to_string(state.minSizeZ);
        uiSec["MaxSizeZ"] = std::to_string(state.maxSizeZ);
        uiSec["Search"] = state.search;
        uiSec["SelectedLot"] = std::to_string(state.selectedLotID);
        std::ostringstream oss;
        for (size_t i=0;i<state.selectedGroups.size();++i){
            if(i) oss << ",";
            oss << "0x" << std::hex << std::uppercase << state.selectedGroups[i] << std::dec;
        }
        uiSec["SelectedGroups"] = oss.str();
        std::ostringstream favoss; for (size_t i=0;i<state.favorites.size();++i){ if(i) favoss << ","; favoss << "0x" << std::hex << std::uppercase << state.favorites[i] << std::dec; }
        uiSec["Favorites"] = favoss.str();
        uiSec["FavoritesOnly"] = state.favoritesOnly ? "1" : "0";
        // MRU persistence removed

        file.write(ini);
    }
}
