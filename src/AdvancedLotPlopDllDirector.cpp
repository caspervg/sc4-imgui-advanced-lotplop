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
// ReSharper disable CppDFAUnreachableCode
#include "version.h"
#include "utils/Logger.h"
#include "cIGZApp.h"
#include "cIGZCheatCodeManager.h"
#include "cIGZCOM.h"
#include "cIGZFrameWork.h"
#include "cIGZMessage2Standard.h"
#include "cIGZMessageServer2.h"
#include "cISC4App.h"
#include "cISC4City.h"
#include "cISC4View3DWin.h"
#include "cRZAutoRefCount.h"
#include "cRZMessage2COMDirector.h"
#include "GZServPtrs.h"
#include "args.hxx"
#include <windows.h>
#include "../vendor/imgui/imgui_impl_win32.h"
#include "../vendor/imgui/imgui_impl_dx11.h"
#include <d3d11.h>

#include "cGZPersistResourceKey.h"
#include "cIGZPersistResourceManager.h"
#include "cISC4Lot.h"
#include "cISC4LotConfiguration.h"
#include "cISC4LotConfigurationManager.h"
#include "SCPropertyUtil.h"
#include "StringResourceKey.h"
#include "StringResourceManager.h"
#include "utils/D3D11Hook.h"
#include "PersistResourceKeyFilterByTypeAndInstance.h"
#include "cIGZPersistResourceKeyList.h"
#include "cIGZVariant.h"
#include "cISCProperty.h"
#include "cIGZCommandServer.h"
#include "cIGZCommandParameterSet.h"
#include "cIGZWin.h"
#include "cRZBaseVariant.h"
#include "PersistResourceKeyFilterByInstance.h"
#include "exemplar/ExemplarUtil.h"
#include "exemplar/PropertyUtil.h"
#include "exemplar/IconResourceUtil.h"
#include "gfx/DX11ImageLoader.h"
#include "ui/LotConfigEntry.h"
#include "ui/AdvancedLotPlopUI.h"
#include "utils/Config.h"

class AdvancedLotPlopDllDirector;
static constexpr uint32_t kMessageCheatIssued = 0x230E27AC;
static constexpr uint32_t kSC4MessagePostCityInit = 0x26D31EC1;
static constexpr uint32_t kSC4MessagePreCityShutdown = 0x26D31EC2;

static constexpr uint32_t kAdvancedLotPlopDirectorID = 0xF78115BE;
// Randomly generated ID to avoid conflicts with other mods

static constexpr uint32_t kLotPlopCheatID = 0x4AC096C6;

AdvancedLotPlopDllDirector *GetLotPlopDirector();

class AdvancedLotPlopDllDirector final : public cRZMessage2COMDirector {
public:
    AdvancedLotPlopDllDirector()
        : pCheatCodeManager(nullptr),
          pCity(nullptr),
          pView3D(nullptr),
          showWindow(true),
          filterZoneType(0xFF),
          filterWealthType(0xFF),
          minSizeX(1), maxSizeX(16),
          minSizeZ(1), maxSizeZ(16),
          selectedLotIID(0) {
        Logger::Initialize("SC4AdvancedLotPlop");
        LOG_INFO("SC4AdvancedLotPlop v{}", PLUGIN_VERSION_STR);

        // Load config (occupant group mapping)
        Config::LoadOnce();

        searchBuffer[0] = '\0';

        // Wire UI callbacks and data pointers
        AdvancedLotPlopUICallbacks cb{};
        cb.OnPlop = [](uint32_t lotID){ if (GetLotPlopDirector()) GetLotPlopDirector()->TriggerLotPlop(lotID); };
        cb.OnBuildCache = [](){ if (GetLotPlopDirector()) GetLotPlopDirector()->BuildLotConfigCache(); };
        cb.OnRefreshList = [](){ if (GetLotPlopDirector()) GetLotPlopDirector()->RefreshLotList(); };
        mUI.SetCallbacks(cb);
        mUI.SetLotEntries(&lotEntries);
    }

    ~AdvancedLotPlopDllDirector() override {
        LOG_INFO("~AdvancedLotPlopDllDirector()");

        // Release cached icon SRVs
        for (auto &kv : lotConfigCache) {
            auto &e = kv.second;
            if (e.iconSRV) {
                e.iconSRV->Release();
                e.iconSRV = nullptr;
            }
        }

        if (imGuiInitialized) {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            imGuiInitialized = false;
        }
        D3D11Hook::Shutdown();

        Logger::Shutdown();
    }

    [[nodiscard]] uint32_t GetDirectorID() const override {
        return kAdvancedLotPlopDirectorID;
    }

    bool OnStart(cIGZCOM *pCOM) override {
        mpFrameWork->AddHook(this);
        return true;
    }

    void PostCityInit(cIGZMessage2Standard *pStandardMsg) {
        cISC4AppPtr pSC4App;
        cIGZMessageServer2Ptr pMessageServer;
        cIGZApp *pApp = mpFrameWork->Application();

        pCity = static_cast<cISC4City *>(pStandardMsg->GetVoid1());
        mUI.SetCity(pCity);
        lotConfigCache.clear();
        cacheInitialized = false;

        if (pSC4App && pMS2) {
            constexpr uint32_t kGZWin_WinSC4App = 0x6104489A;
            constexpr uint32_t kGZWin_SC4View3DWin = 0x9a47b417;
            constexpr uint32_t kGZIID_cISC4View3DWin = 0xFA47B3F9;

            cIGZWin* pMainWindow = pSC4App->GetMainWindow();

            if (pMainWindow) {
                cIGZWin* pWinSC4App = pMainWindow->GetChildWindowFromID(kGZWin_WinSC4App);

                if (pWinSC4App) {
                    if (pWinSC4App->GetChildAs(
                        kGZWin_SC4View3DWin,
                        kGZIID_cISC4View3DWin,
                        reinterpret_cast<void**>(&pView3D))) {
                            LOG_DEBUG("Set pView3D");
                        }
                }
            }
        }

        // Get the game window using Windows API - simpler and more reliable
        if (!imGuiInitialized) {
            // Find the main SimCity 4 window
            HWND hGameWindow = FindWindowA(nullptr, "SimCity 4");

            if (!hGameWindow) {
                // Try getting the active window as fallback
                hGameWindow = GetActiveWindow();
            }

            if (hGameWindow && IsWindow(hGameWindow)) {
                LOG_INFO("Got game window: 0x{:X}", reinterpret_cast<uintptr_t>(hGameWindow));

                ImGui::CreateContext();
                if (D3D11Hook::Initialize(hGameWindow)) {
                    LOG_INFO("D3D11Hook initialized successfully");
                    D3D11Hook::SetPresentCallback(OnImGuiRender); // new signature
                    ImGui_ImplWin32_Init(hGameWindow); // Win32 backend now; DX11 renderer later
                    imGuiInitialized = true;
                } else {
                    LOG_WARN("D3D11Hook failed - ImGui will not be available");
                    ImGui::DestroyContext();
                }
            } else {
                LOG_ERROR("Failed to find SimCity 4 window");
            }
        }

        // Perform initial lot list population
        // RefreshLotList();
    }

    bool PreAppInit() override {
        return true;
    }

    bool PostAppInit() override {
        cISC4AppPtr pSC4App;
        cIGZMessageServer2Ptr pMS2;
        LOG_INFO("PostAppInit: Initializing AdvancedLotPlopDllDirector");

        cIGZApp *const pApp = mpFrameWork->Application();

        if (pApp) {
            cRZAutoRefCount<cISC4App> pSC4App;

            if (pApp->QueryInterface(GZIID_cISC4App, pSC4App.AsPPVoid())) {
                pCheatCodeManager = pSC4App->GetCheatCodeManager();
            }
        }

        if (pMS2) {
            pMS2->AddNotification(this, kSC4MessagePostCityInit);
            pMS2->AddNotification(this, kSC4MessagePreCityShutdown);
            this->pMS2 = pMS2;
        }

        return true;
    }

    void PreCityShutdown(cIGZMessage2Standard *pStandardMsg) {
        lotConfigCache.clear();
        cacheInitialized = false;

        // Ensure UI no longer references city resources during shutdown
        mUI.SetCity(nullptr);

        cISC4View3DWin *localView3D = pView3D;
        pView3D = nullptr;

        if (localView3D) {
            localView3D->Release();
        }
    }

    void RenderUI() {
        // Delegate to UI class
        mUI.Render();
    }

private:
    cIGZCheatCodeManager *pCheatCodeManager;
    cISC4City *pCity;
    cISC4View3DWin *pView3D;
    cIGZMessageServer2 *pMS2;

    // UI facade
    AdvancedLotPlopUI mUI;

    // Legacy state retained for data and integration
    bool showWindow;
    uint8_t filterZoneType;
    uint8_t filterWealthType;
    uint32_t minSizeX, maxSizeX;
    uint32_t minSizeZ, maxSizeZ;
    char searchBuffer[256];
    std::vector<LotConfigEntry> lotEntries;
    uint32_t selectedLotIID;
    std::unordered_map<uint32_t, LotConfigEntry> lotConfigCache;
    bool imGuiInitialized = false;
    bool cacheInitialized = false;

    // Render logic has been moved to src/ui/AdvancedLotPlopUI
    void RenderFilters() {

        // Zone Type
        const char *zoneTypes[] = {"Any", "Residential", "Commercial", "Industrial"};
        int currentZone = (filterZoneType == 0xFF) ? 0 : (filterZoneType + 1);
        if (ImGui::Combo("Zone Type", &currentZone, zoneTypes, IM_ARRAYSIZE(zoneTypes))) {
            filterZoneType = (currentZone == 0) ? 0xFF : (currentZone - 1);
            RefreshLotList();
        }

        // Wealth Type
        const char *wealthTypes[] = {"Any", "Low ($)", "Medium ($$)", "High ($$$)"};
        int currentWealth = (filterWealthType == 0xFF) ? 0 : (filterWealthType + 1);
        if (ImGui::Combo("Wealth", &currentWealth, wealthTypes, IM_ARRAYSIZE(wealthTypes))) {
            filterWealthType = (currentWealth == 0) ? 0xFF : (currentWealth - 1);
            RefreshLotList();
        }

        // Size sliders
        ImGui::Text("Size Range:");
        if (ImGui::SliderInt("Min Width", (int *) &minSizeX, 1, 16) |
            ImGui::SliderInt("Max Width", (int *) &maxSizeX, 1, 16) |
            ImGui::SliderInt("Min Depth", (int *) &minSizeZ, 1, 16) |
            ImGui::SliderInt("Max Depth", (int *) &maxSizeZ, 1, 16)) {
            RefreshLotList();
        }

        // Search
        if (ImGui::InputText("Search", searchBuffer, sizeof(searchBuffer))) {
            RefreshLotList();
        }

        if (ImGui::Button("Clear Filters")) {
            ClearFilters();
            RefreshLotList();
        }
    }

    void RenderLotList() {
        ImGui::Text("Lot Configurations (%zu found)", lotEntries.size());

        if (ImGui::BeginTable("LotTable", 3,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableHeadersRow();

            for (const auto &entry: lotEntries) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);

                bool isSelected = (entry.id == selectedLotIID);
                char label[32];
                snprintf(label, sizeof(label), "0x%08X", entry.id);

                if (ImGui::Selectable(label, isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                    selectedLotIID = entry.id;
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", entry.name.c_str());

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%ux%u", entry.sizeX, entry.sizeZ);
            }

            ImGui::EndTable();
        }
    }

    void RenderDetails() {
        if (selectedLotIID == 0) {
            ImGui::Text("No lot selected");
            return;
        }

        auto it = std::find_if(lotEntries.begin(), lotEntries.end(),
                               [this](const LotConfigEntry &e) { return e.id == selectedLotIID; });

        if (it != lotEntries.end()) {
            ImGui::Text("Selected Lot: %s", it->name.c_str());
            ImGui::Text("ID: 0x%08X", it->id);
            ImGui::Text("Size: %ux%u", it->sizeX, it->sizeZ);

            ImGui::Spacing();
            if (ImGui::Button("Plop")) {
                TriggerLotPlop(selectedLotIID);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Activates the game's built-in plop tool.\nClick in the city to place the building.");
            }
        }
    }

    void BuildLotConfigCache() {
        if (cacheInitialized) return;

        LOG_INFO("Building lot configuration cache...");

        cISC4LotConfigurationManager *pLotConfigMgr = pCity->GetLotConfigurationManager();
        if (!pLotConfigMgr) return;

        cIGZPersistResourceManagerPtr pRM;
        if (!pRM) return;

        constexpr uint32_t kExemplarType = 0x6534284A;
        constexpr uint32_t kPropertyLotObjectsBase = 0x88EDC900;
        constexpr uint32_t kPropertyLotObjectsRange = 256;
        constexpr uint32_t kPropertyUserVisibleNameKey = 0x8A416A99;
        constexpr uint32_t kPropertyExemplarType = 0x00000010;
        constexpr uint32_t kPropertyExemplarTypeBuilding = 0x00000002;

        for (uint32_t x = 1; x <= 16; x++) {
            for (uint32_t z = 1; z <= 16; z++) {
                SC4HashSet<uint32_t> configIdTable{};
                configIdTable.Init(8);

                if (pLotConfigMgr->GetLotConfigurationIDsBySize(configIdTable, x, z)) {
                    for (const auto it: configIdTable) {
                        uint32_t lotConfigID = it->key;

                        if (lotConfigCache.count(lotConfigID)) continue;

                        cISC4LotConfiguration *pConfig = pLotConfigMgr->GetLotConfiguration(lotConfigID);
                        if (!pConfig) continue;

                        LotConfigEntry entry;
                        entry.id = lotConfigID;

                        cRZAutoRefCount<cISCPropertyHolder> pLotExemplar;
                        if (GetExemplarByInstance(pRM, lotConfigID, pLotExemplar)) {
                            uint32_t buildingExemplarID = 0;
                            if (GetLotBuildingExemplarID(pLotExemplar, buildingExemplarID)) {
                                cRZAutoRefCount<cISCPropertyHolder> pBuildingExemplar;
                                if (GetExemplarByInstanceAndType(
                                    pRM,
                                    buildingExemplarID,
                                    kPropertyExemplarType,
                                    kPropertyExemplarTypeBuilding,
                                    pBuildingExemplar
                                )) {
                                    cRZBaseString displayName;
                                    if (PropertyUtil::GetDisplayName(pBuildingExemplar, displayName)) {
                                        cRZBaseString techName;
                                        pConfig->GetName(techName);
                                        entry.name = std::string(displayName.Data()) + " (" + techName.Data() + ")";
                                    }

                                    // Description (Item Description)
                                    {
                                        cRZBaseString desc;
                                        if (PropertyUtil::GetItemDescription(pBuildingExemplar, desc)) {
                                            entry.description = desc.Data();
                                        }
                                    }

                                    // Try to load Item Icon PNG and create SRV
                                    uint32_t iconInstance = 0;
                                    if (ExemplarUtil::GetItemIconInstance(pBuildingExemplar, iconInstance)) {
                                        std::vector<uint8_t> pngBytes;
                                        if (ExemplarUtil::LoadPNGByInstance(pRM, iconInstance, pngBytes)) {
                                            if (!pngBytes.empty()) {
                                                ID3D11Device* device = D3D11Hook::GetDevice();
                                                if (device) {
                                                    ID3D11ShaderResourceView* srv = nullptr;
                                                    int w = 0, h = 0;
                                                    if (gfx::CreateSRVFromPNGMemory(pngBytes.data(), pngBytes.size(), device, &srv, &w, &h)) {
                                                        entry.iconSRV = srv;
                                                        entry.iconWidth = w;
                                                        entry.iconHeight = h;
                                                        LOG_DEBUG("Created SRV for lot config ID 0x{:X}", lotConfigID);
                                                    } else {
                                                        LOG_ERROR("Failed to create SRV from PNG memory");
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    // Occupant Groups
                                    {
                                        constexpr uint32_t kOccupantGroupProperty = 0xAA1DD396;
                                        const cISCProperty* prop = pBuildingExemplar->GetProperty(kOccupantGroupProperty);
                                        if (prop) {
                                            const cIGZVariant* val = prop->GetPropertyValue();
                                            if (val && val->GetType() == cIGZVariant::Type::Uint32Array) {
                                                uint32_t count = val->GetCount();
                                                const uint32_t* pVals = val->RefUint32();
                                                if (pVals && count > 0) {
                                                    for (uint32_t i = 0; i < count; ++i) {
                                                        entry.occupantGroups.insert(pVals[i]);
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        if (entry.name.empty()) {
                            cRZBaseString techName;
                            if (pConfig->GetName(techName)) {
                                entry.name = techName.Data();
                            }
                        }

                        pConfig->GetSize(entry.sizeX, entry.sizeZ);
                        entry.minCapacity = pConfig->GetMinBuildingCapacity();
                        entry.maxCapacity = pConfig->GetMaxBuildingCapacity();
                        entry.growthStage = pConfig->GetGrowthStage();

                        lotConfigCache[lotConfigID] = entry;
                    }
                }
            }
        }

        cacheInitialized = true;
        LOG_INFO("Lot configuration cache built: {} entries", lotConfigCache.size());
    }

    void RefreshLotList() {
        // Build cache if needed
        if (!cacheInitialized) {
            BuildLotConfigCache();
        }

        lotEntries.clear();

        cISC4LotConfigurationManager *pLotConfigMgr = pCity->GetLotConfigurationManager();
        if (!pLotConfigMgr) return;

        // Now just filter from cache - much faster!
        for (uint32_t x = mUI.GetMinSizeX(); x <= mUI.GetMaxSizeX(); x++) {
            for (uint32_t z = mUI.GetMinSizeZ(); z <= mUI.GetMaxSizeZ(); z++) {
                SC4HashSet<uint32_t> configIdTable{};
                configIdTable.Init(8);

                if (pLotConfigMgr->GetLotConfigurationIDsBySize(configIdTable, x, z)) {
                    for (const auto it: configIdTable) {
                        auto cacheIt = lotConfigCache.find(it->key);
                        if (cacheIt == lotConfigCache.end()) continue;

                        const LotConfigEntry &cachedEntry = cacheIt->second;

                        // Apply filters
                        cISC4LotConfiguration *pConfig = pLotConfigMgr->GetLotConfiguration(it->key);
                        if (!pConfig || !MatchesFilters(pConfig)) continue;

                        // Search filter
                        if (mUI.GetSearchBuffer()[0] != '\0') {
                            std::string searchLower = mUI.GetSearchBuffer();
                            std::transform(searchLower.begin(), searchLower.end(),
                                           searchLower.begin(),
                                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                            std::string nameLower = cachedEntry.name;
                            std::transform(nameLower.begin(), nameLower.end(),
                                           nameLower.begin(),
                                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                            // Match against name OR description
                            if (nameLower.find(searchLower) == std::string::npos) {
                                std::string descLower = cachedEntry.description;
                                std::transform(descLower.begin(), descLower.end(),
                                               descLower.begin(),
                                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                                if (descLower.find(searchLower) == std::string::npos) continue;
                            }
                        }

                        // Occupant group filter (Any-match)
                        const auto& selGroups = mUI.GetSelectedOccupantGroups();
                        if (!selGroups.empty()) {
                            bool any = false;
                            for (uint32_t g : selGroups) {
                                if (cachedEntry.occupantGroups.count(g) != 0) {
                                    any = true; break;
                                }
                            }
                            if (!any) continue;
                        }

                        lotEntries.push_back(cachedEntry);
                    }
                }
            }
        }
    }

    void ToggleWindow() {
        bool* pShow = mUI.GetShowWindowPtr();
        *pShow = !*pShow;
        if (*pShow) {
            // Only rebuild cache if city changed or first time
            if (!cacheInitialized) {
                BuildLotConfigCache();
            }
            RefreshLotList();
        }
    }

    bool MatchesFilters(cISC4LotConfiguration *pConfig) {
        // Zone filter: UI exposes R/C/I categories, not exact zone densities.
        // Map UI value (0=R,1=C,2=I) to the set of densities and check compatibility with any of them.
        if (mUI.GetFilterZoneType() != 0xFF) {
            const uint8_t zoneCategory = mUI.GetFilterZoneType();
            bool zoneOk = false;
            if (zoneCategory == 0) {
                // Residential: low, medium, high
                cISC4ZoneManager::ZoneType resZones[] = {
                    cISC4ZoneManager::ZoneType::ResidentialLowDensity,
                    cISC4ZoneManager::ZoneType::ResidentialMediumDensity,
                    cISC4ZoneManager::ZoneType::ResidentialHighDensity,
                };
                for (auto z : resZones) {
                    if (pConfig->IsCompatibleWithZoneType(z)) { zoneOk = true; break; }
                }
            } else if (zoneCategory == 1) {
                // Commercial: low, medium, high
                cISC4ZoneManager::ZoneType comZones[] = {
                    cISC4ZoneManager::ZoneType::CommercialLowDensity,
                    cISC4ZoneManager::ZoneType::CommercialMediumDensity,
                    cISC4ZoneManager::ZoneType::CommercialHighDensity,
                };
                for (auto z : comZones) {
                    if (pConfig->IsCompatibleWithZoneType(z)) { zoneOk = true; break; }
                }
            } else if (zoneCategory == 2) {
                // Industrial: medium, high (Agriculture is separate and not included in generic I)
                cISC4ZoneManager::ZoneType indZones[] = {
                    cISC4ZoneManager::ZoneType::IndustrialMediumDensity,
                    cISC4ZoneManager::ZoneType::IndustrialHighDensity,
                };
                for (auto z : indZones) {
                    if (pConfig->IsCompatibleWithZoneType(z)) { zoneOk = true; break; }
                }
            } else if (zoneCategory == 3) {
                // Agriculture
                if (pConfig->IsCompatibleWithZoneType(cISC4ZoneManager::ZoneType::Agriculture)) zoneOk = true;
            } else if (zoneCategory == 4) {
                // Plopped
                if (pConfig->IsCompatibleWithZoneType(cISC4ZoneManager::ZoneType::Plopped)) zoneOk = true;
            } else if (zoneCategory == 5) {
                // None
                if (pConfig->IsCompatibleWithZoneType(cISC4ZoneManager::ZoneType::None)) zoneOk = true;
            } else if (zoneCategory == 6) {
                // Other: Military, Airport, Seaport, Spaceport, Landfill
                cISC4ZoneManager::ZoneType otherZones[] = {
                    cISC4ZoneManager::ZoneType::Military,
                    cISC4ZoneManager::ZoneType::Airport,
                    cISC4ZoneManager::ZoneType::Seaport,
                    cISC4ZoneManager::ZoneType::Spaceport,
                    cISC4ZoneManager::ZoneType::Landfill,
                };
                for (auto z : otherZones) {
                    if (pConfig->IsCompatibleWithZoneType(z)) { zoneOk = true; break; }
                }
            }
            if (!zoneOk) return false;
        }

        // Wealth filter: UI stores 0=Low,1=Medium,2=High; enum is 1..3
        if (mUI.GetFilterWealthType() != 0xFF) {
            const uint8_t wealthIdx = mUI.GetFilterWealthType();
            cISC4BuildingOccupant::WealthType wealthType =
                static_cast<cISC4BuildingOccupant::WealthType>(wealthIdx + 1);
            if (!pConfig->IsCompatibleWithWealthType(wealthType)) return false;
        }

        return true;
    }

    void ClearFilters() {
        filterZoneType = 0xFF;
        filterWealthType = 0xFF;
        minSizeX = 1;
        maxSizeX = 16;
        minSizeZ = 1;
        maxSizeZ = 16;
        searchBuffer[0] = '\0';
    }

    void TriggerLotPlop(uint32_t lotID) {
        // Use command 0xec3e82f8 (same as keyboard shortcuts) to activate placement tool
        if (!pView3D) {
            LOG_WARN("pView3D not available for lot placement");
            return;
        }

        cIGZCommandServerPtr pCmdServer;
        if (!pCmdServer) {
            LOG_ERROR("Command server not available");
            return;
        }

        // Create parameter sets
        cIGZCommandParameterSet* pInput = nullptr;
        cIGZCommandParameterSet* pOutput = nullptr;

        if (!pCmdServer->CreateCommandParameterSet(&pInput) ||
            !pCmdServer->CreateCommandParameterSet(&pOutput)) {
            LOG_ERROR("Failed to create command parameter sets");
            return;
        }

        // Set building ID as uint32 array in parameter 0 (handler expects array)
        auto var = cRZBaseVariant();
        uint32_t lotArray[2] = { lotID, true }; // Second argument is true
        var.RefUint32(lotArray, 2);  // Array of 1 element
        pInput->SetParameter(0, var);

        // Send placement command (0xec3e82f8 = activate building placement tool)
        bool success = pView3D->ProcessCommand(0xec3e82f8, *pInput, *pOutput);

        if (success) {
            LOG_INFO("Activated placement tool for lot 0x{:X}", lotID);
        } else {
            LOG_WARN("Failed to activate placement tool for lot 0x{:X}", lotID);
        }

        // Cleanup
        pInput->Release();
        pOutput->Release();
    }

    bool DoMessage(cIGZMessage2 *pMsg) {
        auto pStandardMsg = static_cast<cIGZMessage2Standard *>(pMsg);

        switch (pMsg->GetType()) {
            case kMessageCheatIssued:
                // ProcessCheat(pStandardMsg);
                break;
            case kSC4MessagePostCityInit:
                PostCityInit(pStandardMsg);
                break;
            case kSC4MessagePreCityShutdown:
                PreCityShutdown(pStandardMsg);
                break;
            default:
                LOG_DEBUG("Unsupported message type: 0x{:X}", pMsg->GetType());
                break;
        }

        return true;
    }

    static void OnImGuiRender(
        ID3D11Device *pDevice,
        ID3D11DeviceContext *pContext,
        IDXGISwapChain *pSwapChain) {
        static bool initialized = false;
        static ID3D11RenderTargetView *pRTV = nullptr;

        // If ImGui context has been destroyed, skip rendering safely
        if (ImGui::GetCurrentContext() == nullptr) {
            return;
        }

        // Lazy initialize renderer backend
        if (!initialized) {
            HWND hWnd = D3D11Hook::GetGameWindow();
            if (hWnd && pDevice && pContext) {
                ImGui_ImplDX11_Init(pDevice, pContext);
                initialized = true;
                LOG_INFO("ImGui DX11 backend initialized in render callback");
            }
        }
        if (!initialized) return;

        // Create RTV once from swap chain
        if (!pRTV && pSwapChain) {
            ID3D11Texture2D *pBackBuffer = nullptr;
            HRESULT hr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&pBackBuffer));
            if (SUCCEEDED(hr) && pBackBuffer) {
                pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &pRTV);
                pBackBuffer->Release();
                LOG_INFO("Created DX11 render target view for ImGui");
            }
        }
        if (pRTV) {
            pContext->OMSetRenderTargets(1, &pRTV, nullptr);
        }

        // Frame setup
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        auto pDirector = GetLotPlopDirector();
        if (pDirector) {
            pDirector->RenderUI();
        }

        // Render
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }
};

static AdvancedLotPlopDllDirector sDirector;

cRZCOMDllDirector *RZGetCOMDllDirector() {
    return &sDirector;
}

AdvancedLotPlopDllDirector *GetLotPlopDirector() {
    return &sDirector;
}
