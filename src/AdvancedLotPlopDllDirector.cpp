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
#include "cIGZFrameWorkW32.h"
#include "cIGZMessage2Standard.h"
#include "cIGZMessageServer2.h"
#include "cISC4App.h"
#include "cISC4City.h"
#include "cISC4View3DWin.h"
#include "cRZAutoRefCount.h"
#include "cRZBaseString.h"
#include "cRZMessage2COMDirector.h"
#include "GZServPtrs.h"
#include "args.hxx"
#include <windows.h>
#include <ddraw.h>
#include "../vendor/imgui/imgui_impl_win32.h"
#include "../vendor/imgui/imgui_impl_dx7.h"

#include "cGZPersistResourceKey.h"
#include "cIGZPersistResourceManager.h"
#include "cIGZWinKeyAcceleratorRes.h"
#include "cISC4Lot.h"
#include "cISC4LotConfiguration.h"
#include "cISC4LotConfigurationManager.h"
// #include "utils/D3D11Hook.h" // Removed: migrating to DX7 backend
#include "cIGZVariant.h"
#include "cISCProperty.h"
#include "cIGZCommandServer.h"
#include "cIGZCommandParameterSet.h"
#include "cIGZWin.h"
#include "cRZBaseVariant.h"
#include "ui/LotConfigEntry.h"
#include "ui/AdvancedLotPlopUI.h"
#include "utils/Config.h"
#include "cache/LotCacheManager.h"
#include "filter/LotFilterer.h"
#include <string>

#include "utils/D3D7Hook.h"

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
          pView3D(nullptr) {
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
        AdvancedLotPlopUICallbacks cb{};
        cb.OnPlop = [](uint32_t lotID) { if (GetLotPlopDirector()) GetLotPlopDirector()->TriggerLotPlop(lotID); };
        cb.OnBuildCache = []() { if (GetLotPlopDirector()) GetLotPlopDirector()->BuildCache(); };
        cb.OnRefreshList = []() { if (GetLotPlopDirector()) GetLotPlopDirector()->RefreshLotList(); };
        mUI.SetCallbacks(cb);
        mUI.SetLotEntries(&lotEntries);
    }

    ~AdvancedLotPlopDllDirector() override {
        LOG_INFO("~AdvancedLotPlopDllDirector()");

        lotCacheManager.Clear();

        if (imGuiInitialized) {
            ImGui_ImplDX7_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            imGuiInitialized = false;
        }
        D3D7Hook::Shutdown();

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

        // Get the game window from framework
        if (!imGuiInitialized) {
            HWND hGameWindow = nullptr;

            // Query framework for Windows-specific interface
            cRZAutoRefCount<cIGZFrameWorkW32> pFrameworkW32;
            if (mpFrameWork->QueryInterface(GZIID_cIGZFrameWorkW32, pFrameworkW32.AsPPVoid())) {
            	if (!pFrameworkW32)
            	{
            		LOG_ERROR("Failed to get framework W32 interface");
            		return;
            	}
                hGameWindow = pFrameworkW32->GetMainHWND();
            }

            if (hGameWindow && IsWindow(hGameWindow)) {
                LOG_INFO("Got game window from framework: 0x{:X}", reinterpret_cast<uintptr_t>(hGameWindow));

                ImGui::CreateContext();
                if (D3D7Hook::Initialize(hGameWindow)) {
                    LOG_INFO("D3D7Hook initialized successfully");
                    D3D7Hook::SetRenderCallback(OnImGuiRender);
                    ImGui_ImplWin32_Init(hGameWindow);
                    imGuiInitialized = true;
                } else {
                    LOG_WARN("D3D7Hook failed - ImGui will not be available");
                    ImGui::DestroyContext();
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
        // Unregister our shortcut notifications
        UnregisterToggleShortcut();

        lotCacheManager.Clear();

        // Ensure UI no longer references city resources during shutdown
        mUI.SetCity(nullptr);

        cISC4View3DWin *localView3D = pView3D;
        pView3D = nullptr;

        if (localView3D) {
            localView3D->Release();
        }
    }

    void Update() {
        // Handle pending cache build (deferred to allow loading window to render first)
        if (pendingCacheBuild) {
            pendingCacheBuild = false;

            cIGZPersistResourceManagerPtr pRM;
            IDirectDraw7* pDDraw = D3D7Hook::GetDDraw();

            auto progressCallback = [](const char* stage, int current, int total) {
                if (GetLotPlopDirector()) {
                    GetLotPlopDirector()->UpdateLoadingProgress(stage, current, total);
                }
            };

            lotCacheManager.BuildCache(pCity, pRM, pDDraw, progressCallback);
            mUI.ShowLoadingWindow(false);
            // Now that the cache is ready, immediately refresh the filtered list
            RefreshLotList();
        }
    }

    void RenderUI() {
        bool *pShow = mUI.GetShowWindowPtr();
        if (pShow && *pShow) {
            // Delegate to UI class
            mUI.Render();
        }
    }

private:
    cIGZCheatCodeManager *pCheatCodeManager;
    cISC4City *pCity;
    cISC4View3DWin *pView3D;
    cIGZMessageServer2 *pMS2;

    // Services
    LotCacheManager lotCacheManager;

    // UI facade
    AdvancedLotPlopUI mUI;

    // Filtered lot list (populated by RefreshLotList)
    std::vector<LotConfigEntry> lotEntries;

    bool imGuiInitialized = false;
    bool pendingCacheBuild = false;

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

    void BuildCache() {
        mUI.ShowLoadingWindow(true);
        mUI.SetLoadingProgress("Initializing...", 0, 0);
        pendingCacheBuild = true;
    }

    void UpdateLoadingProgress(const char* stage, int current, int total) {
        mUI.SetLoadingProgress(stage, current, total);
    }

    // Delegate to filterer
    void RefreshLotList() {
        if (!lotCacheManager.IsInitialized()) {
            BuildCache();
            return; // Defer filtering until cache is ready
        }

        LotFilterer::FilterLots(
            pCity,
            lotCacheManager.GetLotConfigCache(),
            lotEntries,
            mUI.GetFilterZoneType(),
            mUI.GetFilterWealthType(),
            mUI.GetMinSizeX(), mUI.GetMaxSizeX(),
            mUI.GetMinSizeZ(), mUI.GetMaxSizeZ(),
            mUI.GetSearchBuffer(),
            mUI.GetSelectedOccupantGroups()
        );
    }

    void ToggleWindow() {
    	pView3D->RemoveAllViewInputControls(false);
        bool *pShow = mUI.GetShowWindowPtr();
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
            // uint32_t* ptr = reinterpret_cast<uint32_t*>(storedParam);
            // uint16_t* shortPtr = reinterpret_cast<uint16_t*>(storedParam);
            //
            // ptr[1] = lotID;       // Data at offset +0x04
            // shortPtr[7] = 0x0006; // Type=6 (Uint32) at offset +0x0E

        	storedParam->SetValUint32(lotID);
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
	    IDirect3DDevice7* pDevice,
	    IDirectDrawSurface7* pPrimarySurface) {
        static bool initialized = false;

        // If ImGui context has been destroyed, skip rendering safely
        if (ImGui::GetCurrentContext() == nullptr) {
            return;
        }

        // Lazy initialize renderer backend
        if (!initialized) {
            HWND hWnd = D3D7Hook::GetGameWindow();
            IDirectDraw7* pDDraw = D3D7Hook::GetDDraw();
            if (hWnd && pDevice && pDDraw) {
                ImGui_ImplDX7_Init(pDevice, pDDraw);
                initialized = true;
                LOG_INFO("ImGui DX7 backend initialized in render callback");
            }
        }
        if (!initialized) return;

        // Frame setup
        ImGui_ImplDX7_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        auto pDirector = GetLotPlopDirector();
        if (pDirector) {
            pDirector->Update();   // Business logic update
            pDirector->RenderUI(); // Pure rendering
        }

        // Render
        ImGui::Render();
        ImGui_ImplDX7_RenderDrawData(ImGui::GetDrawData());
    }
};

static AdvancedLotPlopDllDirector sDirector;

cRZCOMDllDirector *RZGetCOMDllDirector() {
    return &sDirector;
}

AdvancedLotPlopDllDirector *GetLotPlopDirector() {
    return &sDirector;
}
