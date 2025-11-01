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
#include <windows.h>
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include "cGZPersistResourceKey.h"
#include "cIGZPersistResourceManager.h"
#include "cIGZWinKeyAcceleratorRes.h"
#include "cISC4Lot.h"
#include "cISC4LotConfiguration.h"
#include "cISC4LotConfigurationManager.h"
#include "utils/D3D11Hook.h"
#include "cIGZVariant.h"
#include "cISCProperty.h"
#include "cIGZCommandServer.h"
#include "cIGZCommandParameterSet.h"
#include "cIGZWin.h"
#include "cRZBaseVariant.h"
#include "exemplar/ExemplarUtil.h"
#include "exemplar/PropertyUtil.h"
#include "exemplar/IconResourceUtil.h"
#include "gfx/DX11ImageLoader.h"
#include "ui/LotConfigEntry.h"
#include "ui/AdvancedLotPlopUI.h"
#include "utils/Config.h"
#include "utils/CacheSerializer.h"
#include "s3d/S3DReader.h"
#include "s3d/S3DRenderer.h"
#include <future>
#include <deque>
#include <string>

#include "cIGZPersistDBRecord.h"
#include "cIGZPersistResourceKeyList.h"
#include "SC4HashSet.h"

class AdvancedLotPlopDllDirector;
static constexpr uint32_t kMessageCheatIssued = 0x230E27AC;
static constexpr uint32_t kSC4MessagePostCityInit = 0x26D31EC1;
static constexpr uint32_t kSC4MessagePreCityShutdown = 0x26D31EC2;

static constexpr uint32_t kAdvancedLotPlopDirectorID = 0xF78115BE;
// Randomly generated ID to avoid conflicts with other mods

static constexpr uint32_t kLotPlopCheatID = 0x4AC096C6;

// Hotkey/message ID to toggle the ImGui window (unique)
static constexpr uint32_t kToggleLotPlopWindowShortcutID = 0x9F21C3A1;

// Private KeyConfig resource to register our hotkey accelerators (same pattern as BulldozeExtensions)
static constexpr uint32_t kKeyConfigType = 0xA2E3D533;
static constexpr uint32_t kKeyConfigGroup = 0x8F1E6D69;
static constexpr uint32_t kKeyConfigInstance = 0x5CBCFBF8;

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
        cb.OnPlop = [](uint32_t lotID) { if (GetLotPlopDirector()) GetLotPlopDirector()->TriggerLotPlop(lotID); };
        cb.OnBuildCache = []() { if (GetLotPlopDirector()) GetLotPlopDirector()->BuildLotConfigCache(); };
        cb.OnRefreshList = []() { if (GetLotPlopDirector()) GetLotPlopDirector()->RefreshLotList(); };
        cb.OnRequestIcon = [](uint32_t iconInstanceID) {
            if (GetLotPlopDirector()) GetLotPlopDirector()->RequestIcon(iconInstanceID);
        };
        mUI.SetCallbacks(cb);
        mUI.SetLotEntries(&lotEntries);
    }

    ~AdvancedLotPlopDllDirector() override {
        LOG_INFO("~AdvancedLotPlopDllDirector()");

        // Detach async cache build future to avoid blocking during shutdown
        if (buildFuture.valid()) {
            LOG_INFO("Detaching cache build future during shutdown");
            // Move to static container so future doesn't block destruction
            static std::vector<std::future<void> > detachedFutures;
            detachedFutures.push_back(std::move(buildFuture));
        }

        // Release cached icon SRVs
        for (auto &kv: lotConfigCache) {
            auto &e = kv.second;
            if (e.iconSRV) {
                e.iconSRV->Release();
                e.iconSRV = nullptr;
            }
        }

        lotConfigCache.clear();
        exemplarCache.clear();

        // Clean up S3D thumbnail
        if (s3dThumbnailSRV) {
            s3dThumbnailSRV->Release();
            s3dThumbnailSRV = nullptr;
        }
        s3dRenderer.reset();

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

            cIGZWin *pMainWindow = pSC4App->GetMainWindow();

            if (pMainWindow) {
                cIGZWin *pWinSC4App = pMainWindow->GetChildWindowFromID(kGZWin_WinSC4App);

                if (pWinSC4App) {
                    if (pWinSC4App->GetChildAs(
                        kGZWin_SC4View3DWin,
                        kGZIID_cISC4View3DWin,
                        reinterpret_cast<void **>(&pView3D))) {
                        RegisterToggleShortcut();
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
        // Unregister our shortcut notifications
        UnregisterToggleShortcut();

        // Detach any running cache build future
        if (buildFuture.valid()) {
            LOG_INFO("Detaching cache build future during city shutdown");
            static std::vector<std::future<void> > detachedFutures;
            detachedFutures.push_back(std::move(buildFuture));
        }
        buildStarted = false;
        isLoading = false;

        lotConfigCache.clear();
        exemplarCache.clear();
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
        bool *pShow = mUI.GetShowWindowPtr();
        if (pShow && *pShow) {
            if (isLoading) {
                ImGui::OpenPopup("Loading lots...");
                bool open = true;
                if (ImGui::BeginPopupModal("Loading lots...", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("Building lot cache, please wait...");
                    ImGui::Separator();
                    ImGui::TextDisabled("This may take some time, but is only required the first time.");
                    ImGui::TextDisabled("Current cache size: %d", lotConfigCache.size());
                    ImGui::EndPopup();
                }

                // Poll async build completion without blocking the render thread
                if (buildStarted && buildFuture.valid()) {
                    auto status = buildFuture.wait_for(std::chrono::milliseconds(0));
                    if (status == std::future_status::ready) {
                        // Ensure completion and then refresh UI list once
                        try { buildFuture.get(); } catch (...) {
                            /* swallow to avoid crashing UI */
                        }
                        RefreshLotList();
                        isLoading = false;
                        buildStarted = false;
                        ImGui::CloseCurrentPopup();
                    }
                }
            } else {
                // Run a small number of lazy icon decode jobs per frame
                ProcessIconJobs(128);
                // Delegate to UI class
                mUI.Render();

                // Render S3D thumbnail proof-of-concept window
                RenderS3DThumbnailWindow();
            }
        }
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
    // Map visible list row by lot id for quick updates when icons load
    std::unordered_map<uint32_t, size_t> lotEntryIndexByID;

    // Exemplar cache: instance ID -> vector of (group, exemplar) pairs
    // Pre-loaded at city init to avoid thousands of DAT file searches
    std::unordered_map<uint32_t, std::vector<std::pair<uint32_t, cRZAutoRefCount<cISCPropertyHolder> > > >
    exemplarCache;

    // Lazy icon decode queue
    std::deque<uint32_t> iconJobQueue;

    bool imGuiInitialized = false;
    bool cacheInitialized = false;
    bool isLoading = false;
    bool buildStarted = false;
    std::future<void> buildFuture;

    // S3D thumbnail proof-of-concept
    bool showS3DThumbnail = true;
    ID3D11ShaderResourceView *s3dThumbnailSRV = nullptr;
    std::unique_ptr<S3D::Renderer> s3dRenderer;
    bool s3dThumbnailGenerated = false;

    void RequestIcon(uint32_t lotID);

    void ProcessIconJobs(uint32_t maxJobsPerFrame = 1);

    // S3D thumbnail generation
    void GenerateS3DThumbnail(ID3D11Device *device, ID3D11DeviceContext *context);

    void RenderS3DThumbnailWindow();

    // Cache persistence
    bool LoadCacheFromDisk(const std::string &path);

    void SaveCacheToDisk(const std::string &path);

    void RegisterToggleShortcut() {
        if (!pView3D) return;
        cIGZPersistResourceManagerPtr pRM;
        if (pRM) {
            cRZAutoRefCount<cIGZWinKeyAcceleratorRes> pAcceleratorRes;
            const cGZPersistResourceKey key(kKeyConfigType, kKeyConfigGroup, kKeyConfigInstance);
            if (pRM->GetPrivateResource(key, kGZIID_cIGZWinKeyAcceleratorRes, pAcceleratorRes.AsPPVoid(), 0, nullptr)) {
                auto *pAccel = pView3D->GetKeyAccelerator();
                if (pAccel) {
                    pAcceleratorRes->RegisterResources(pAccel);
                    if (pMS2) {
                        pMS2->AddNotification(this, kToggleLotPlopWindowShortcutID);
                    }
                }
            }
        }
    }

    void UnregisterToggleShortcut() {
        cIGZMessageServer2Ptr pMS2Local;
        if (pMS2Local) {
            pMS2Local->RemoveNotification(this, kToggleLotPlopWindowShortcutID);
        }
    }

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

    // Build exemplar cache: loads all exemplars once to avoid repeated DAT file searches
    void BuildExemplarCache() {
        if (!exemplarCache.empty()) return;

        LOG_INFO("Building exemplar cache...");

        cIGZPersistResourceManagerPtr pRM;
        if (!pRM) return;

        constexpr uint32_t kExemplarType = 0x6534284A;

        cRZAutoRefCount<cIGZPersistResourceKeyList> pKeyList;
        uint32_t totalCount = pRM->GetAvailableResourceList(pKeyList.AsPPObj(), nullptr);

        if (totalCount == 0 || !pKeyList) {
            LOG_WARN("Failed to enumerate resources for exemplar cache");
            return;
        }

        LOG_INFO("Filtering {} resources for exemplars...", totalCount);

        uint32_t exemplarCount = 0;
        for (int i = 0; i < pKeyList->Size(); i++) {
            auto key = pKeyList->GetKey(i);

            // Only cache exemplars (0x6534284A), skip PNGs, LTEXTs, etc.
            if (key.type != kExemplarType) continue;
            LOG_INFO("Found: {}/{} - Examplar - 0x{:08X} - {}", i + 1, totalCount, key.instance, key.group);

            cRZAutoRefCount<cISCPropertyHolder> exemplar;
            if (pRM->GetResource(key, GZIID_cISCPropertyHolder, exemplar.AsPPVoid(), 0, nullptr)) {
                // Store by instance ID -> vector of (group, exemplar) pairs
                exemplarCache[key.instance].emplace_back(key.group, exemplar);
                LOG_INFO("Added to exemplar cache: {}/{} - Examplar - 0x{:08X} - {}", i + 1, totalCount, key.instance,
                         key.group);
                exemplarCount++;
            }
        }

        LOG_INFO("Exemplar cache built: {} exemplars across {} unique instance IDs", exemplarCount,
                 exemplarCache.size());
    }

    // Cached version of GetExemplarByInstance - returns first match for given instance ID
    bool GetCachedExemplar(uint32_t instanceID, cRZAutoRefCount<cISCPropertyHolder> &outExemplar) {
        auto it = exemplarCache.find(instanceID);
        if (it != exemplarCache.end() && !it->second.empty()) {
            outExemplar = it->second[0].second;
            return true;
        }
        return false;
    }

    // Cached version of GetExemplarByInstanceAndType - finds matching exemplar with specific type
    bool GetCachedExemplarByType(
        uint32_t instanceID,
        uint32_t exemplarTypePropertyID,
        uint32_t expectedTypeValue,
        cRZAutoRefCount<cISCPropertyHolder> &outExemplar
    ) {
        auto it = exemplarCache.find(instanceID);
        if (it != exemplarCache.end()) {
            for (auto &[group, exemplar]: it->second) {
                uint32_t actualType;
                if (GetPropertyUint32(exemplar, exemplarTypePropertyID, actualType) && actualType ==
                    expectedTypeValue) {
                    outExemplar = exemplar;
                    return true;
                }
            }
        }
        return false;
    }

    void BuildLotConfigCache() {
        if (cacheInitialized) return;

        LOG_INFO("Building lot configuration cache...");

        cISC4LotConfigurationManager *pLotConfigMgr = pCity->GetLotConfigurationManager();
        if (!pLotConfigMgr) return;

        cIGZPersistResourceManagerPtr pRM;
        if (!pRM) return;

        // Pre-allocate capacity to avoid rehashing during population
        lotConfigCache.reserve(2048);

        constexpr uint32_t kExemplarType = 0x6534284A;
        constexpr uint32_t kPropertyLotObjectsBase = 0x88EDC900;
        constexpr uint32_t kPropertyLotObjectsRange = 256;
        constexpr uint32_t kPropertyUserVisibleNameKey = 0x8A416A99;
        constexpr uint32_t kPropertyExemplarType = 0x00000010;
        constexpr uint32_t kPropertyExemplarTypeBuilding = 0x00000002;

        // Reuse hash set across iterations to avoid repeated allocations
        SC4HashSet<uint32_t> configIdTable{};
        configIdTable.Init(256);

        for (uint32_t x = 1; x <= 16; x++) {
            for (uint32_t z = 1; z <= 16; z++) {
                if (pLotConfigMgr->GetLotConfigurationIDsBySize(configIdTable, x, z)) {
                    for (const auto it: configIdTable) {
                        uint32_t lotConfigID = it->key;

                        if (lotConfigCache.count(lotConfigID)) continue;

                        cISC4LotConfiguration *pConfig = pLotConfigMgr->GetLotConfiguration(lotConfigID);
                        if (!pConfig) continue;

                        LotConfigEntry entry;
                        entry.id = lotConfigID;

                        cRZAutoRefCount<cISCPropertyHolder> pLotExemplar;
                        if (GetCachedExemplar(lotConfigID, pLotExemplar)) {
                            uint32_t buildingExemplarID = 0;
                            if (GetLotBuildingExemplarID(pLotExemplar, buildingExemplarID)) {
                                cRZAutoRefCount<cISCPropertyHolder> pBuildingExemplar;
                                if (GetCachedExemplarByType(
                                    buildingExemplarID,
                                    kPropertyExemplarType,
                                    kPropertyExemplarTypeBuilding,
                                    pBuildingExemplar
                                )) {
                                    cRZBaseString displayName;
                                    if (PropertyUtil::GetDisplayName(pBuildingExemplar, displayName)) {
                                        cRZBaseString techName;
                                        pConfig->GetName(techName);
                                        // Optimize string concatenation with reserve
                                        entry.name.reserve(displayName.Strlen() + techName.Strlen() + 3);
                                        entry.name = displayName.Data();
                                        entry.name += " (";
                                        entry.name += techName.Data();
                                        entry.name += ")";
                                    }

                                    cRZBaseString desc;
                                    if (PropertyUtil::GetItemDescription(pBuildingExemplar, desc)) {
                                        entry.description = desc.Data();
                                    }

                                    // Store Item Icon PNG instance ID for lazy loading later
                                    uint32_t iconInstance = 0;
                                    if (ExemplarUtil::GetItemIconInstance(pBuildingExemplar, iconInstance)) {
                                        entry.iconInstance = iconInstance;
                                    }

                                    // Occupant Groups
                                    {
                                        constexpr uint32_t kOccupantGroupProperty = 0xAA1DD396;
                                        const cISCProperty *prop = pBuildingExemplar->GetProperty(
                                            kOccupantGroupProperty);
                                        if (prop) {
                                            const cIGZVariant *val = prop->GetPropertyValue();
                                            if (val && val->GetType() == cIGZVariant::Type::Uint32Array) {
                                                uint32_t count = val->GetCount();
                                                const uint32_t *pVals = val->RefUint32();
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
        lotEntryIndexByID.clear();

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
                        const auto &selGroups = mUI.GetSelectedOccupantGroups();
                        if (!selGroups.empty()) {
                            bool any = false;
                            for (uint32_t g: selGroups) {
                                if (cachedEntry.occupantGroups.count(g) != 0) {
                                    any = true;
                                    break;
                                }
                            }
                            if (!any) continue;
                        }

                        lotEntries.push_back(cachedEntry);
                        lotEntryIndexByID[cachedEntry.id] = lotEntries.size() - 1;
                    }
                }
            }
        }
    }

    static std::string GetModuleDir() {
        char path[MAX_PATH] = {0};
        HMODULE hMod = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR) &GetModuleDir,
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

    void ToggleWindow() {
        static const int kCacheSchemaVersion = 1;
        const std::string cacheFile = GetModuleDir().append("\\AdvancedLotPlopCache.ini");
        bool *pShow = mUI.GetShowWindowPtr();
        *pShow = !*pShow;
        if (*pShow) {
            // On first open after city init, build cache lazily with loading screen
            if (!cacheInitialized) {
                // Try to load from on-disk cache first for instant startup
                if (LoadCacheFromDisk(cacheFile)) {
                    cacheInitialized = true;
                    LOG_INFO("Loaded lot cache from disk: {} entries", lotConfigCache.size());
                    RefreshLotList();
                } else {
                    // Fall back to building asynchronously with a loading popup
                    isLoading = true;
                    if (!buildStarted) {
                        buildStarted = true;
                        buildFuture = std::async(std::launch::async, [this, cacheFile]() {
                            // Build exemplar cache first - required for fast lot cache building
                            BuildExemplarCache();
                            // Now build lot cache using cached exemplars
                            BuildLotConfigCache();
                            // Persist for next run
                            SaveCacheToDisk(cacheFile);
                        });
                    }
                }
            } else {
                RefreshLotList();
            }
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
                for (auto z: resZones) {
                    if (pConfig->IsCompatibleWithZoneType(z)) {
                        zoneOk = true;
                        break;
                    }
                }
            } else if (zoneCategory == 1) {
                // Commercial: low, medium, high
                cISC4ZoneManager::ZoneType comZones[] = {
                    cISC4ZoneManager::ZoneType::CommercialLowDensity,
                    cISC4ZoneManager::ZoneType::CommercialMediumDensity,
                    cISC4ZoneManager::ZoneType::CommercialHighDensity,
                };
                for (auto z: comZones) {
                    if (pConfig->IsCompatibleWithZoneType(z)) {
                        zoneOk = true;
                        break;
                    }
                }
            } else if (zoneCategory == 2) {
                // Industrial: medium, high (Agriculture is separate and not included in generic I)
                cISC4ZoneManager::ZoneType indZones[] = {
                    cISC4ZoneManager::ZoneType::IndustrialMediumDensity,
                    cISC4ZoneManager::ZoneType::IndustrialHighDensity,
                };
                for (auto z: indZones) {
                    if (pConfig->IsCompatibleWithZoneType(z)) {
                        zoneOk = true;
                        break;
                    }
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
                for (auto z: otherZones) {
                    if (pConfig->IsCompatibleWithZoneType(z)) {
                        zoneOk = true;
                        break;
                    }
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
        if (!pView3D) return;

        cIGZCommandServerPtr pCmdServer;
        if (!pCmdServer) return;

        cIGZCommandParameterSet *pCmd1 = nullptr;
        cIGZCommandParameterSet *pCmd2 = nullptr;

        if (!pCmdServer->CreateCommandParameterSet(&pCmd1) || !pCmd1 ||
            !pCmdServer->CreateCommandParameterSet(&pCmd2) || !pCmd2) {
            if (pCmd1) pCmd1->Release();
            if (pCmd2) pCmd2->Release();
            return;
        }

        // Create a fake variant in pCmd1, this will get clobbered in AppendParameter anyway
        cRZBaseVariant dummyVariant;
        dummyVariant.SetValUint32(0);
        pCmd1->AppendParameter(dummyVariant);

        // Get the game's internal variant and patch it directly
        cIGZVariant *storedParam = pCmd1->GetParameter(0);
        if (storedParam) {
            uint32_t *ptr = reinterpret_cast<uint32_t *>(storedParam);
            uint16_t *shortPtr = reinterpret_cast<uint16_t *>(storedParam);

            ptr[1] = lotID; // Data at offset +0x04
            shortPtr[7] = 0x0006; // Type=6 (Uint32) at offset +0x0E
        }

        pView3D->ProcessCommand(0xec3e82f8, *pCmd1, *pCmd2);

        if (bool *pShow = mUI.GetShowWindowPtr()) {
            *pShow = false;
        }

        pCmd1->Release();
        pCmd2->Release();
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
            case kToggleLotPlopWindowShortcutID:
                ToggleWindow();
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
            // Generate S3D thumbnail if not done yet
            if (!pDirector->s3dThumbnailGenerated && pDirector->pCity) {
                pDirector->GenerateS3DThumbnail(pDevice, pContext);
            }

            pDirector->RenderUI();
        }

        // Render
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }
};

// Lazy icon: enqueue request if not yet queued/loaded
void AdvancedLotPlopDllDirector::RequestIcon(uint32_t lotID) {
    auto it = lotConfigCache.find(lotID);
    if (it == lotConfigCache.end()) return;
    LotConfigEntry &entry = it->second;
    if (entry.iconSRV || entry.iconRequested) return;
    if (entry.iconInstance == 0) {
        // no icon to load
        entry.iconRequested = true; // prevent repeated requests
        return;
    }
    entry.iconRequested = true;
    iconJobQueue.push_back(lotID);
}

void AdvancedLotPlopDllDirector::ProcessIconJobs(uint32_t maxJobsPerFrame) {
    if (!cacheInitialized || iconJobQueue.empty() || maxJobsPerFrame == 0) return;

    uint32_t processed = 0;
    while (processed < maxJobsPerFrame && !iconJobQueue.empty()) {
        uint32_t lotID = iconJobQueue.front();
        iconJobQueue.pop_front();

        auto it = lotConfigCache.find(lotID);
        if (it == lotConfigCache.end()) { continue; }
        LotConfigEntry &entry = it->second;
        if (entry.iconSRV) { continue; }

        // Resource manager
        cIGZPersistResourceManagerPtr pRM;
        if (!pRM) { continue; }

        // Use stored icon instance to avoid walking exemplar chain
        if (entry.iconInstance == 0) {
            continue; // no icon for this entry
        }
        std::vector<uint8_t> pngBytes;
        if (!ExemplarUtil::LoadPNGByInstance(pRM, entry.iconInstance, pngBytes) || pngBytes.empty()) {
            continue;
        }

        ID3D11Device *device = D3D11Hook::GetDevice();
        if (!device) { continue; }

        ID3D11ShaderResourceView *srv = nullptr;
        int w = 0, h = 0;
        if (gfx::CreateSRVFromPNGMemory(pngBytes.data(), pngBytes.size(), device, &srv, &w, &h)) {
            // Update cache entry
            entry.iconSRV = srv;
            entry.iconWidth = w;
            entry.iconHeight = h;
            // Propagate to visible list if present
            auto mit = lotEntryIndexByID.find(lotID);
            if (mit != lotEntryIndexByID.end()) {
                size_t idx = mit->second;
                if (idx < lotEntries.size()) {
                    lotEntries[idx].iconSRV = srv;
                    lotEntries[idx].iconWidth = w;
                    lotEntries[idx].iconHeight = h;
                }
            }
        }

        processed++;
    }
}

static AdvancedLotPlopDllDirector sDirector;

cRZCOMDllDirector *RZGetCOMDllDirector() {
    return &sDirector;
}

AdvancedLotPlopDllDirector *GetLotPlopDirector() {
    return &sDirector;
}

void AdvancedLotPlopDllDirector::SaveCacheToDisk(const std::string &path) {
    // Delegate to INI serializer util
    CacheSerializer::SaveLotCacheINI(lotConfigCache, path, PLUGIN_VERSION_STR, 1);
}

bool AdvancedLotPlopDllDirector::LoadCacheFromDisk(const std::string &path) {
    // Delegate to INI serializer util
    return CacheSerializer::LoadLotCacheINI(lotConfigCache, path, 1);
}

void AdvancedLotPlopDllDirector::GenerateS3DThumbnail(ID3D11Device *device, ID3D11DeviceContext *context) {
    if (s3dThumbnailGenerated || !device || !context) {
        return;
    }

    LOG_INFO("Generating S3D thumbnail proof-of-concept...");

    // Test prop: T=0x6534284a, G=0xc977c536, I=0x1d830000
    constexpr uint32_t EXEMPLAR_TYPE = 0x6534284a;
    // constexpr uint32_t S3D_GROUP = 0xf38748f8;
    // constexpr uint32_t S3D_INSTANCE = 0x0445bb45;
    constexpr uint32_t EXEMPLAR_GROUP = 0x13a0bd51;
    constexpr uint32_t EXEMPLAR_ISNTANCE = 0xd6136b79;
	// constexpr uint32_t S3D_GROUP = 0x9dd7f9c4;
	// constexpr uint32_t S3D_INSTANCE = 0x5f9944bd;

    try {
        cIGZPersistResourceManagerPtr pRM;
        if (!pRM) {
            LOG_ERROR("Failed to get resource manager");
            return;
        }

        // Get exemplar resource from ResourceManager
        cGZPersistResourceKey exemplarKey(EXEMPLAR_TYPE, EXEMPLAR_GROUP, EXEMPLAR_ISNTANCE);

        // First get the prop exemplar
        cRZAutoRefCount<cISCPropertyHolder> propExemplar;
        if (!pRM->GetResource(exemplarKey, GZIID_cISCPropertyHolder, propExemplar.AsPPVoid(), 0, nullptr)) {
            LOG_ERROR("Failed to get prop exemplar");
            return;
        }

        // Extract S3D resource key from exemplar
        constexpr uint32_t kResourceKeyType1 = 0x27812821;
        cGZPersistResourceKey s3dKey;
        // Helper function to extract resource key from RKT1 property (Uint32 array with 3 values: T, G, I)
        auto GetPropertyResourceKey = [](cISCPropertyHolder *holder, uint32_t propID,
                                         cGZPersistResourceKey &outKey) -> bool {
            const cISCProperty *prop = holder->GetProperty(propID);
            if (!prop) return false;

            const cIGZVariant *val = prop->GetPropertyValue();
            if (!val || val->GetType() != cIGZVariant::Type::Uint32Array) return false;

            uint32_t count = val->GetCount();
            if (count < 3) return false;

            const uint32_t *vals = val->RefUint32();
            if (!vals) return false;

            outKey.type = vals[0];
            outKey.group = vals[1];
            outKey.instance = vals[2];
            return true;
        };

        auto res = GetPropertyResourceKey(propExemplar, kResourceKeyType1, s3dKey);

        if (!res) {
            LOG_ERROR("Failed to get S3D resource key from prop exemplar");
            return;
        }

        s3dKey.instance += 0x400; // Highest zoom level
        LOG_DEBUG("Using zoom 5 S3D instance: 0x{:08X}", s3dKey.instance);

        cIGZPersistDBRecord* pRecord = nullptr;
        if (!pRM->OpenDBRecord(s3dKey, &pRecord, false)) {
            LOG_ERROR("Failed to open S3D record - TGI may not exist: {:08x}-{:08x}-{:08x}",
                      s3dKey.type, s3dKey.group, s3dKey.instance);
            return;
        }

        uint32_t dataSize = pRecord->GetSize();
        if (dataSize == 0) {
            LOG_ERROR("S3D record has zero size");
            pRecord->Close();
            return;
        }

        std::vector<uint8_t> s3dData(dataSize);
        if (!pRecord->GetFieldVoid(s3dData.data(), dataSize)) {
            LOG_ERROR("Failed to read S3D data");
            pRecord->Close();
            return;
        }

        // Parse S3D model
        S3D::Model model;
        if (!S3D::Reader::Parse(s3dData.data(), dataSize, model)) {
            LOG_ERROR("Failed to parse S3D model");
            return;
        }

        LOG_INFO("S3D model parsed successfully: {} meshes, {} frames",
                 model.animation.animatedMeshes.size(), model.animation.frameCount);

        // Create renderer
        s3dRenderer = std::make_unique<S3D::Renderer>(device, context);

        // Load model into renderer (using ResourceManager for texture loading)
        if (!s3dRenderer->LoadModel(model, pRM, s3dKey.group)) {
            LOG_ERROR("Failed to load S3D model into renderer");
            s3dRenderer.reset();
            return;
        }

        // Generate thumbnail
        s3dThumbnailSRV = s3dRenderer->GenerateThumbnail(256);
        if (s3dThumbnailSRV) {
            LOG_INFO("S3D thumbnail generated successfully!");
            s3dThumbnailGenerated = true;
        } else {
            LOG_ERROR("Failed to generate S3D thumbnail");
            s3dRenderer.reset();
        }
    } catch (const std::exception &e) {
        LOG_ERROR("Exception while generating S3D thumbnail: {}", e.what());
        s3dRenderer.reset();
    }
}

void AdvancedLotPlopDllDirector::RenderS3DThumbnailWindow() {
    if (!showS3DThumbnail) {
        return;
    }

    if (ImGui::Begin("S3D Thumbnail Proof-of-Concept", &showS3DThumbnail)) {
        ImGui::Text("Testing S3D rendering with prop:");
        ImGui::Text("Type:     0x6534284a");
        ImGui::Text("Group:    0xc977c536");
        ImGui::Text("Instance: 0x1d830000");
        ImGui::Separator();

        if (s3dThumbnailSRV) {
            ImGui::Text("Thumbnail generated:");
            ImGui::Image((ImTextureID) s3dThumbnailSRV, ImVec2(256, 256));
        } else if (s3dThumbnailGenerated) {
            ImGui::Text("Failed to generate thumbnail");
        } else {
            ImGui::Text("Thumbnail not yet generated");
            if (ImGui::Button("Generate Thumbnail")) {
                // Trigger generation on next render (when we have device/context)
                // This will be called from OnImGuiRender where we have access to device
                s3dThumbnailGenerated = false; // Reset flag to trigger generation
            }
        }
    }
    ImGui::End();
}
