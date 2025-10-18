#include "Config.h"
#include <windows.h>
#include <mINI/ini.h>
#include <mutex>

namespace Config {
    static std::once_flag g_once;

    static std::unordered_map<uint32_t, std::string>& GetGroupNamesMap() {
        static std::unordered_map<uint32_t, std::string> g_groupNames{};
        return g_groupNames;
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
}
