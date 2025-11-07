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
#include <d3d11.h>
#include <filesystem>
#include <string>
#include <windows.h>

#include "cGZPersistResourceKey.h"
#include "cIGZApp.h"
#include "cIGZCheatCodeManager.h"
#include "cIGZCOM.h"
#include "cIGZCommandParameterSet.h"
#include "cIGZCommandServer.h"
#include "cIGZFrameWork.h"
#include "cIGZFrameWorkW32.h"
#include "cIGZMessage2Standard.h"
#include "cIGZMessageServer2.h"
#include "cIGZVariant.h"
#include "cIGZWin.h"
#include "cISC4App.h"
#include "cISC4City.h"
#include "cISC4View3DWin.h"
#include "cRZAutoRefCount.h"
#include "cRZBaseString.h"
#include "cRZBaseVariant.h"
#include "cRZMessage2COMDirector.h"
#include "GZServPtrs.h"
#include "imgui_impl_win32.h"
#include "version.h"
#include "cache/LotCacheBuildOrchestrator.h"
#include "cache/LotCacheManager.h"
#include "cache/PropCacheBuildOrchestrator.h"
#include "cache/PropCacheManager.h"
#include "lots/AdvancedLotPlopUI.h"
#include "lots/LotConfigEntry.h"
#include "lots/LotFilterer.h"
#include "props/PropPainterControlManager.h"
#include "props/PropPainterUI.h"
#include "s3d/S3DRenderer.h"
#include "utils/Config.h"
#include "utils/D3D11Hook.h"
#include "utils/ImGuiLifecycleManager.h"
#include "utils/Logger.h"
#include "utils/ShortcutManager.h"

class AdvancedLotPlopDllDirector;
static constexpr uint32_t kMessageCheatIssued = 0x230E27AC;
static constexpr uint32_t kSC4MessagePostCityInit = 0x26D31EC1;
static constexpr uint32_t kSC4MessagePreCityShutdown = 0x26D31EC2;

static constexpr uint32_t kAdvancedLotPlopDirectorID = 0xF78115BE;
// Randomly generated ID to avoid conflicts with other mods

static constexpr uint32_t kLotPlopCheatID = 0x4AC096C6;

// Hotkey/message ID to toggle the ImGui window (unique)
static constexpr uint32_t kToggleLotPlopWindowShortcutID = 0x9F21C3A1;
static constexpr uint32_t kTogglePropPainterWindowShortcutID = 0x8B4A7F2E;

// Private KeyConfig resource to register our hotkey accelerators (same pattern as BulldozeExtensions)
static constexpr uint32_t kKeyConfigType = 0xA2E3D533;
static constexpr uint32_t kKeyConfigGroup = 0x8F1E6D69;
static constexpr uint32_t kKeyConfigInstance = 0x5CBCFBF8;

AdvancedLotPlopDllDirector *GetLotPlopDirector();

class AdvancedLotPlopDllDirector final : public cRZMessage2COMDirector {
public:
    AdvancedLotPlopDllDirector()
        : lotCacheBuildOrchestrator(lotCacheManager, mLotPlopUI),
          propCacheBuildOrchestrator(propCacheManager, mPropPaintUI),
          propPainterControlManager(propCacheManager, mPropPaintUI) {
        std::string userDir;
        cISC4AppPtr pSC4App;
        if (pSC4App) {
            cRZBaseString userDirStr;
            if (pSC4App->GetUserDataDirectory(userDirStr)) {
                userDir = userDirStr.Data();
            }
        }

        Logger::Initialize("SC4AdvancedLotPlop", userDir);
        LOG_INFO("SC4AdvancedLotPlop v{}", PLUGIN_VERSION_STR);

        // Load config (occupant group mapping)
        Config::LoadOnce();

        // Wire UI callbacks
        AdvancedLotPlopUICallbacks lotPlopCb{};
        lotPlopCb.OnPlop = [](uint32_t lotID) { if (GetLotPlopDirector()) GetLotPlopDirector()->TriggerLotPlop(lotID); };
        lotPlopCb.OnBuildCache = []() { if (GetLotPlopDirector()) GetLotPlopDirector()->BuildCache(); };
        lotPlopCb.OnRefreshList = []() { if (GetLotPlopDirector()) GetLotPlopDirector()->RefreshLotList(); };
        mLotPlopUI.SetCallbacks(lotPlopCb);
        mLotPlopUI.SetLotEntries(&lotEntries);

        // Wire prop painter UI callbacks
        PropPainterUICallbacks propPaintCb{};
        propPaintCb.OnStartPainting = [this](uint32_t propID, int rotation) {
            propPainterControlManager.StartPainting(propID, rotation, pCity, pView3D);
        };
        propPaintCb.OnStopPainting = [this]() {
            propPainterControlManager.StopPainting(pView3D);
        };
        propPaintCb.OnBuildCache = []() {
            if (GetLotPlopDirector()) GetLotPlopDirector()->BuildPropCache();
        };
        mPropPaintUI.SetCallbacks(propPaintCb);
        mPropPaintUI.SetPropCacheManager(&propCacheManager);
    }

    ~AdvancedLotPlopDllDirector() override {
        LOG_INFO("~AdvancedLotPlopDllDirector()");

        lotCacheManager.Clear();
        propCacheManager.Clear();

        imGuiLifecycle.Shutdown();
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
        mLotPlopUI.SetCity(pCity);

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
                        // Register shortcuts
                        shortcutManager.RegisterShortcuts(
                            pView3D,
                            pMS2,
                            this,
                            {kToggleLotPlopWindowShortcutID, kTogglePropPainterWindowShortcutID}
                        );
                    }
                }
            }
        }

        // Initialize ImGui if not already done
        if (!imGuiLifecycle.IsWin32Initialized()) {
            HWND hGameWindow = nullptr;

            // Query framework for Windows-specific interface
            cRZAutoRefCount<cIGZFrameWorkW32> pFrameworkW32;
            if (mpFrameWork->QueryInterface(GZIID_cIGZFrameWorkW32, pFrameworkW32.AsPPVoid())) {
            	if (!pFrameworkW32) {
            		LOG_ERROR("Failed to get framework W32 interface");
            		return;
            	}
                hGameWindow = pFrameworkW32->GetMainHWND();
            }

            if (hGameWindow && IsWindow(hGameWindow)) {
                LOG_INFO("Got game window from framework: 0x{:X}", reinterpret_cast<uintptr_t>(hGameWindow));

                // Initialize D3D11 hook and ImGui Win32 backend
                if (D3D11Hook::Initialize(hGameWindow)) {
                    LOG_INFO("D3D11Hook initialized successfully");
                    D3D11Hook::SetPresentCallback(OnImGuiRender);
                    imGuiLifecycle.InitializeWin32(hGameWindow);
                } else {
                    LOG_WARN("D3D11Hook failed - ImGui will not be available");
                }
            } else {
                LOG_ERROR("Failed to get game window from framework");
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
        // Unregister shortcut notifications
        shortcutManager.UnregisterShortcuts(
            this,
            {kToggleLotPlopWindowShortcutID, kTogglePropPainterWindowShortcutID}
        );

        // Cancel any pending incremental cache build
        if (lotCacheBuildOrchestrator.IsBuilding()) {
            lotCacheBuildOrchestrator.Cancel();
        }

        // Save cache to database before clearing
        std::string userProfile = std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") : "";
        if (!userProfile.empty()) {
            std::filesystem::path dbPath = std::filesystem::path(userProfile) / "Documents" / "SimCity 4" / "Plugins" / "AdvancedLotPlopCache.sqlite";

            // Get device and context from D3D11Hook
            ID3D11Device* pDevice = D3D11Hook::GetDevice();
            ID3D11DeviceContext* pContext = D3D11Hook::GetContext();

            if (pDevice && pContext) {
                // Save lot cache if initialized
                if (lotCacheManager.IsInitialized()) {
                    lotCacheManager.SaveToDatabase(dbPath, pDevice, pContext);
                }

                // Save prop cache if initialized
                if (propCacheManager.IsInitialized()) {
                    propCacheManager.SaveToDatabase(dbPath, pDevice, pContext);
                }
            }
        }

        lotCacheManager.Clear();
        propCacheManager.Clear();

        // Ensure UI no longer references city resources during shutdown
        mLotPlopUI.SetCity(nullptr);

        // Reset COM pointers (cRZAutoRefCount handles Release() automatically)
        pCity = nullptr;
        pView3D = nullptr;
    }

    void Update() {
        // Update lot cache build if in progress
        if (lotCacheBuildOrchestrator.IsBuilding()) {
            bool stillBuilding = lotCacheBuildOrchestrator.Update();

            // If build just completed, refresh the lot list
            if (!stillBuilding) {
                RefreshLotList();
            }
        }

        // Update prop cache build if in progress
        if (propCacheBuildOrchestrator.IsBuilding()) {
            propCacheBuildOrchestrator.Update();
        }
    }

    void RenderUI() {
        bool *pShow = mLotPlopUI.GetShowWindowPtr();
        if (pShow && *pShow) {
            // Delegate to UI class
            mLotPlopUI.Render();
        }

        // Render prop painter window independently
        bool *pShowPropPainter = mPropPaintUI.GetShowWindowPtr();
        if (pShowPropPainter && *pShowPropPainter) {
            mPropPaintUI.Render();
        }

        // Render prop painter preview overlay (crosshair, info, etc.)
        mPropPaintUI.RenderPreviewOverlay();
    }

private:
    cRZAutoRefCount<cIGZCheatCodeManager> pCheatCodeManager;
    cRZAutoRefCount<cISC4City> pCity;
    cISC4View3DWin* pView3D;
    cRZAutoRefCount<cIGZMessageServer2> pMS2;

    // Services
    LotCacheManager lotCacheManager;
    PropCacheManager propCacheManager;

    // UI facade
    AdvancedLotPlopUI mLotPlopUI;
    PropPainterUI mPropPaintUI;

    // Orchestrators
    LotCacheBuildOrchestrator lotCacheBuildOrchestrator;
    PropCacheBuildOrchestrator propCacheBuildOrchestrator;

    // ImGui lifecycle manager
    ImGuiLifecycleManager imGuiLifecycle;

    // Shortcut manager
    ShortcutManager shortcutManager{kKeyConfigType, kKeyConfigGroup, kKeyConfigInstance};

    // Prop painter control manager
    PropPainterControlManager propPainterControlManager;

    // Filtered lot list (populated by RefreshLotList)
    std::vector<LotConfigEntry> lotEntries;

    void BuildCache() {
        // Try loading from cache database first
        std::string userProfile = std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") : "";
        if (!userProfile.empty()) {
            std::filesystem::path dbPath = std::filesystem::path(userProfile) / "Documents" / "SimCity 4" / "Plugins" / "AdvancedLotPlopCache.sqlite";

            // Get device and context from D3D11Hook
            ID3D11Device* pDevice = D3D11Hook::GetDevice();
            ID3D11DeviceContext* pContext = D3D11Hook::GetContext();

            if (pDevice && pContext) {
                if (lotCacheManager.LoadFromDatabase(dbPath, pDevice, pContext)) {
                    LOG_INFO("Successfully loaded lot cache from database");
                    RefreshLotList(); // Refresh UI to show loaded lots
                    return; // Cache loaded successfully, no need to rebuild
                } else {
                    LOG_INFO("Failed to load lot cache from database, building from scratch");
                }
            }
        }

        // Fall back to incremental build
        lotCacheBuildOrchestrator.StartBuildCache(pCity);
    }

    // Delegate to filterer
    void RefreshLotList() {
        if (!lotCacheManager.IsInitialized()) {
            BuildCache();
            return; // Defer filtering until the cache is ready
        }

        LOG_DEBUG("RefreshLotList: cache has {} lots", lotCacheManager.GetLotConfigCache().size());

        LotFilterer::FilterLots(
            pCity,
            lotCacheManager.GetLotConfigCache(),
            lotEntries,
            mLotPlopUI.GetFilterZoneType(),
            mLotPlopUI.GetFilterWealthType(),
            mLotPlopUI.GetMinSizeX(), mLotPlopUI.GetMaxSizeX(),
            mLotPlopUI.GetMinSizeZ(), mLotPlopUI.GetMaxSizeZ(),
            mLotPlopUI.GetSearchBuffer(),
            mLotPlopUI.GetSelectedOccupantGroups()
        );

        LOG_DEBUG("RefreshLotList: filtered to {} lots", lotEntries.size());
    }

    void ToggleWindow() {
    	pView3D->RemoveAllViewInputControls(false);
        bool *pShow = mLotPlopUI.GetShowWindowPtr();
        *pShow = !*pShow;
        if (*pShow) {
            if (!lotCacheManager.IsInitialized()) {
                BuildCache();
            }
            RefreshLotList();
        }
    }

    void TriggerLotPlop(uint32_t lotID) {
        if (!pView3D) return;

        cIGZCommandServerPtr pCmdServer;
        if (!pCmdServer) return;

        cIGZCommandParameterSet* pCmd1 = nullptr;
        cIGZCommandParameterSet* pCmd2 = nullptr;

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
        cIGZVariant* storedParam = pCmd1->GetParameter(0);
        if (storedParam) {
        	storedParam->SetValUint32(lotID);
        }

        pView3D->ProcessCommand(0xec3e82f8, *pCmd1, *pCmd2);
    	mLotPlopUI.RegisterPlop(lotID);

        if (bool *pShow = mLotPlopUI.GetShowWindowPtr()) {
            *pShow = false;
        }

        pCmd1->Release();
        pCmd2->Release();
    }

    void BuildPropCache() {
        // Try loading from cache database first
        std::string userProfile = std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") : "";
        if (!userProfile.empty()) {
            std::filesystem::path dbPath = std::filesystem::path(userProfile) / "Documents" / "SimCity 4" / "Plugins" / "AdvancedLotPlopCache.sqlite";

            // Get device and context from D3D11Hook
            ID3D11Device* pDevice = D3D11Hook::GetDevice();
            ID3D11DeviceContext* pContext = D3D11Hook::GetContext();

            if (pDevice && pContext) {
                if (propCacheManager.LoadFromDatabase(dbPath, pDevice, pContext)) {
                    LOG_INFO("Successfully loaded prop cache from database");
                    return; // Cache loaded successfully, no need to rebuild
                } else {
                    LOG_INFO("Failed to load prop cache from database, building from scratch");
                }
            }
        }

        // Fall back to incremental build
        propCacheBuildOrchestrator.StartBuildCache(pCity);
    }

    void TogglePropPainterWindow() {
    	pView3D->RemoveAllViewInputControls(false);

        bool *pShow = mPropPaintUI.GetShowWindowPtr();
        *pShow = !*pShow;

        if (*pShow) {
            // Build cache if not initialized
            if (!propCacheManager.IsInitialized()) {
                BuildPropCache();
            }
        } else {
            // Stop painting when window is closed
            if (propPainterControlManager.IsPainting()) {
                propPainterControlManager.StopPainting(pView3D);
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
            case kTogglePropPainterWindowShortcutID:
        		LOG_DEBUG("Toggle prop painter window");
                TogglePropPainterWindow();
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
        static ID3D11RenderTargetView *pRTV = nullptr;

        auto pDirector = GetLotPlopDirector();
        if (!pDirector) return;

        // If ImGui context has been destroyed, skip rendering safely
        if (ImGui::GetCurrentContext() == nullptr) {
            return;
        }

        // Lazy initialize DX11 backend if needed
        if (!pDirector->imGuiLifecycle.IsDX11Initialized()) {
            if (pDevice && pContext) {
                pDirector->imGuiLifecycle.InitializeDX11(pDevice, pContext);
            }
        }
        if (!pDirector->imGuiLifecycle.IsFullyInitialized()) return;

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

    	if (pDevice && pContext) {
    		pDirector->propCacheBuildOrchestrator.SetDeviceContext(pDevice, pContext);
    		pDirector->lotCacheBuildOrchestrator.SetDeviceContext(pDevice, pContext);
    	}

        // Begin ImGui frame
        pDirector->imGuiLifecycle.BeginFrame();

        // Business logic and UI rendering
        pDirector->Update();
        pDirector->RenderUI();

        // End ImGui frame
        pDirector->imGuiLifecycle.EndFrame();
    }
};

static AdvancedLotPlopDllDirector sDirector;

cRZCOMDllDirector *RZGetCOMDllDirector() {
    return &sDirector;
}

AdvancedLotPlopDllDirector *GetLotPlopDirector() {
    return &sDirector;
}
