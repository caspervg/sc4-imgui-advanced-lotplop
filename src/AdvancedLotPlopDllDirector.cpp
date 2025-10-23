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
#include "../vendor/imgui/imgui.h"

#include "cIGZWin.h"
#include "ui/LotConfigEntry.h"
#include "ui/AdvancedLotPlopUI.h"
#include "utils/Config.h"
#include "services/LotCacheManager.h"
#include "services/LotFilterService.h"
#include "services/WindowManager.h"
#include "services/LotPlopService.h"
#include <future>
#include <string>

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
          pCacheManager(nullptr),
          pFilterService(nullptr),
          pWindowManager(nullptr),
          pPlopService(nullptr),
          isLoading(false),
          buildStarted(false) {
        Logger::Initialize("SC4AdvancedLotPlop");
        LOG_INFO("SC4AdvancedLotPlop v{}", PLUGIN_VERSION_STR);

        // Load config (occupant group mapping)
        Config::LoadOnce();

        // Wire UI callbacks and data pointers
        AdvancedLotPlopUICallbacks cb{};
        cb.OnPlop = [](uint32_t lotID) { if (GetLotPlopDirector()) GetLotPlopDirector()->OnPlopRequested(lotID); };
        cb.OnBuildCache = []() { if (GetLotPlopDirector()) GetLotPlopDirector()->OnBuildCacheRequested(); };
        cb.OnRefreshList = []() { if (GetLotPlopDirector()) GetLotPlopDirector()->OnRefreshListRequested(); };
        cb.OnRequestIcon = nullptr; // Icons loaded immediately during cache build
        mUI.SetCallbacks(cb);
        mUI.SetLotEntries(&lotEntries);
    }

    ~AdvancedLotPlopDllDirector() override {
        LOG_INFO("~AdvancedLotPlopDllDirector()");

        // Release cached icon SRVs
        if (pCacheManager) {
            auto& cache = pCacheManager->GetLotConfigCache();
            for (auto& kv : cache) {
                auto& e = kv.second;
                if (e.iconSRV) {
                    e.iconSRV->Release();
                    e.iconSRV = nullptr;
                }
            }
        }

        // Cleanup services
        delete pCacheManager;
        delete pFilterService;
        delete pWindowManager;
        delete pPlopService;

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
        cIGZApp *pApp = mpFrameWork->Application();

        pCity = static_cast<cISC4City *>(pStandardMsg->GetVoid1());
        mUI.SetCity(pCity);

        // Initialize services
        if (!pCacheManager) {
            pCacheManager = new LotCacheManager(pCity);
        }
        if (!pFilterService) {
            pFilterService = new LotFilterService(pCity, &mUI);
        }
        if (!pWindowManager) {
            pWindowManager = new WindowManager();
        }

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
                        pWindowManager->RegisterToggleShortcut(pView3D, pMS2, this);

                        // Initialize lot plop service with View3D
                        if (!pPlopService) {
                            pPlopService = new LotPlopService(pView3D);
                        }
                    }
                }
            }
        }

        // Initialize ImGui through WindowManager
        if (!pWindowManager->IsImGuiInitialized()) {
            if (pWindowManager->InitializeImGui()) {
                // Set render callback
                WindowManager::SetRenderCallback([this](ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc) {
                    this->RenderUI();
                });
            }
        }
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
        // Unregister shortcuts via WindowManager
        if (pWindowManager) {
            pWindowManager->UnregisterToggleShortcut(pMS2, this);
        }

        // Cancel any running cache build
        if (pCacheManager) {
            pCacheManager->CancelAsyncBuild();
        }
        buildStarted = false;
        isLoading = false;

        // Clear all caches and services
        if (pCacheManager) {
            pCacheManager->Clear();
        }

        lotEntries.clear();
        lotEntryIndexByID.clear();

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
                    if (pCacheManager) {
                        ImGui::TextDisabled("Current cache size: %d", pCacheManager->GetLotConfigCache().size());
                    }
                    ImGui::EndPopup();
                }

                // Poll async build completion without blocking the render thread
                if (buildStarted && pCacheManager && pCacheManager->IsAsyncBuildInProgress()) {
                    // Still building, keep waiting
                } else if (buildStarted) {
                    // Build complete, refresh list
                    OnRefreshListRequested();
                    isLoading = false;
                    buildStarted = false;
                    ImGui::CloseCurrentPopup();
                }
            } else {
                // Delegate to UI class
                mUI.Render();
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

    // Services
    LotCacheManager* pCacheManager;
    LotFilterService* pFilterService;
    WindowManager* pWindowManager;
    LotPlopService* pPlopService;

    // Shared state for UI
    std::vector<LotConfigEntry> lotEntries;
    std::unordered_map<uint32_t, size_t> lotEntryIndexByID;

    // Loading state
    bool isLoading;
    bool buildStarted;

    // Callback handlers (delegate to services)
    void OnPlopRequested(uint32_t lotID);
    void OnBuildCacheRequested();
    void OnRefreshListRequested();

    // Utility methods
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
        bool *pShow = mUI.GetShowWindowPtr();
        *pShow = !*pShow;
        if (*pShow && pCacheManager) {
            // On first open after city init, build cache with loading screen
            if (!pCacheManager->IsCacheInitialized()) {
                isLoading = true;
                if (!buildStarted) {
                    buildStarted = true;
                    pCacheManager->BuildLotConfigCacheAsync([this]() {
                        // Cache built with icons loaded
                    });
                }
            } else {
                OnRefreshListRequested();
            }
        }
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
};

// Callback handler implementations
void AdvancedLotPlopDllDirector::OnPlopRequested(uint32_t lotID) {
    if (pPlopService) {
        pPlopService->TriggerLotPlop(lotID);
        // Close the UI window after plopping
        if (bool* pShow = mUI.GetShowWindowPtr()) {
            *pShow = false;
        }
    }
}

void AdvancedLotPlopDllDirector::OnBuildCacheRequested() {
    if (pCacheManager) {
        pCacheManager->BuildLotConfigCache();
        pCacheManager->SetCacheInitialized(true);
        OnRefreshListRequested();
    }
}

void AdvancedLotPlopDllDirector::OnRefreshListRequested() {
    if (pFilterService && pCacheManager) {
        pFilterService->RefreshLotList(pCacheManager->GetLotConfigCache(), lotEntries, lotEntryIndexByID);
    }
}

static AdvancedLotPlopDllDirector sDirector;

cRZCOMDllDirector *RZGetCOMDllDirector() {
    return &sDirector;
}

AdvancedLotPlopDllDirector *GetLotPlopDirector() {
    return &sDirector;
}
