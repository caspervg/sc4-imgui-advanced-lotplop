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
#include "cIGZPersistResourceManager.h"
#include "cIGZVariant.h"
#include "cIGZWin.h"
#include "cIGZWinKeyAcceleratorRes.h"
#include "cISC4App.h"
#include "cISC4City.h"
#include "cISC4View3DWin.h"
#include "cRZAutoRefCount.h"
#include "cRZBaseString.h"
#include "cRZBaseVariant.h"
#include "cRZMessage2COMDirector.h"
#include "GZServPtrs.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "version.h"
#include "cache/LotCacheManager.h"
#include "filter/LotFilterer.h"
#include "ui/AdvancedLotPlopUI.h"
#include "ui/LotConfigEntry.h"
#include "utils/Config.h"
#include "utils/D3D11Hook.h"
#include "utils/Logger.h"
#include "s3d/S3DReader.h"
#include "s3d/S3DRenderer.h"
#include <deque>
#include "cIGZPersistDBRecord.h"
#include "cISCProperty.h"
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
                if (D3D11Hook::Initialize(hGameWindow)) {
                    LOG_INFO("D3D11Hook initialized successfully");
                    D3D11Hook::SetPresentCallback(OnImGuiRender);
                    ImGui_ImplWin32_Init(hGameWindow); // Win32 backend now; DX11 renderer later
                    imGuiInitialized = true;
                } else {
                    LOG_WARN("D3D11Hook failed - ImGui will not be available");
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

        // Cancel any pending incremental cache build
        if (isCacheBuilding) {
            LOG_INFO("Cancelling incremental cache build during city shutdown");
            isCacheBuilding = false;
            cacheBuildPhase = CacheBuildPhase::NotStarted;
            mUI.ShowLoadingWindow(false);
        }

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
        if (!isCacheBuilding) return;

        switch (cacheBuildPhase) {
            case CacheBuildPhase::BuildingExemplarCache: {
                // Build exemplar cache synchronously (fast operation)
                LOG_INFO("Building exemplar cache...");
                cIGZPersistResourceManagerPtr pRM;
                lotCacheManager.BeginIncrementalBuild();
                lotCacheManager.BuildExemplarCacheSync(pRM);

                // Move to next phase
                cacheBuildPhase = CacheBuildPhase::BuildingLotConfigCache;
                lotCacheManager.BeginLotConfigProcessing(pCity);
                LOG_INFO("Exemplar cache complete, starting lot config processing");
                break;
            }

            case CacheBuildPhase::BuildingLotConfigCache: {
                // Process lots incrementally (20 per frame)
                constexpr int LOTS_PER_FRAME = 20;

                cIGZPersistResourceManagerPtr pRM;
                ID3D11Device* pDevice = D3D11Hook::GetDevice();

                int processed = lotCacheManager.ProcessLotConfigBatch(pRM, pDevice, LOTS_PER_FRAME);

                // Update progress
                int current = lotCacheManager.GetProcessedLotCount();
                int total = lotCacheManager.GetTotalLotCount();
                mUI.SetLoadingProgress("Processing lot configurations...", current, total);

                // Check if complete
                if (lotCacheManager.IsLotConfigProcessingComplete()) {
                    cacheBuildPhase = CacheBuildPhase::Complete;
                    LOG_INFO("Lot config processing complete");
                }
                break;
            }

            case CacheBuildPhase::Complete: {
                // Finalize cache build
                lotCacheManager.FinalizeIncrementalBuild();
                isCacheBuilding = false;
                mUI.ShowLoadingWindow(false);
                cacheBuildPhase = CacheBuildPhase::NotStarted;

                LOG_INFO("Incremental cache build completed");
                RefreshLotList();
                break;
            }

            default:
                break;
        }
    }

    void RenderUI() {
        bool *pShow = mUI.GetShowWindowPtr();
        if (pShow && *pShow) {
            // Delegate to UI class
            mUI.Render();
        	RenderS3DThumbnailWindow();
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

    // Incremental cache building
    bool isCacheBuilding = false;
    enum class CacheBuildPhase {
        NotStarted,
        BuildingExemplarCache,
        BuildingLotConfigCache,
        Complete
    };
    CacheBuildPhase cacheBuildPhase = CacheBuildPhase::NotStarted;

	// S3D thumbnail proof-of-concept
	bool showS3DThumbnail = true;
	ID3D11ShaderResourceView *s3dThumbnailSRV = nullptr;
	std::unique_ptr<S3D::Renderer> s3dRenderer;
	bool s3dThumbnailGenerated = false;
	int s3dZoomLevel = 5;  // SC4 zoom levels: 1-5 (5 is closest)
	int s3dRotation = 0;   // SC4 rotations: 0-3 (cardinal directions)

	// S3D thumbnail generation
	void GenerateS3DThumbnail(ID3D11Device *device, ID3D11DeviceContext *context);

	void RenderS3DThumbnailWindow();
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
        if (isCacheBuilding) {
            LOG_WARN("Cache build already in progress, ignoring request");
            return;
        }

        LOG_INFO("Starting async cache build");
        mUI.ShowLoadingWindow(true);
        mUI.SetLoadingProgress("Initializing...", 0, 0);
        isCacheBuilding = true;
        cacheBuildPhase = CacheBuildPhase::BuildingExemplarCache;
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
        	storedParam->SetValUint32(lotID);
        }

        pView3D->ProcessCommand(0xec3e82f8, *pCmd1, *pCmd2);
    	mUI.RegisterPlop(lotID);

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
            pDirector->Update();   // Business logic update
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

static AdvancedLotPlopDllDirector sDirector;

cRZCOMDllDirector *RZGetCOMDllDirector() {
    return &sDirector;
}

AdvancedLotPlopDllDirector *GetLotPlopDirector() {
    return &sDirector;
}

void AdvancedLotPlopDllDirector::GenerateS3DThumbnail(ID3D11Device *device, ID3D11DeviceContext *context) {
    if (s3dThumbnailGenerated || !device || !context) {
        return;
    }

    LOG_INFO("Generating S3D thumbnail proof-of-concept...");

    constexpr uint32_t EXEMPLAR_TYPE = 0x6534284a;
    constexpr uint32_t EXEMPLAR_GROUP = 0x13a0bd51;
    constexpr uint32_t EXEMPLAR_ISNTANCE = 0xd6136b79;

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

        // Calculate S3D instance offset based on zoom and rotation
        // Pattern from SC4: instance = base + (zoom-1)*0x100 + rotation*0x10
        // Zoom: 1-5 (1=farthest, 5=closest)
        // Rotation: 0-3 (S, E, N, W cardinal directions)
        uint32_t baseInstance = s3dKey.instance;
        uint32_t zoomOffset = (s3dZoomLevel - 1) * 0x100;
        uint32_t rotationOffset = s3dRotation * 0x10;
        s3dKey.instance = baseInstance + zoomOffset + rotationOffset;

        LOG_INFO("Loading S3D: base=0x{:08X}, zoom={} (+0x{:03X}), rot={} (+0x{:02X}), final=0x{:08X}",
                 baseInstance, s3dZoomLevel, zoomOffset, s3dRotation, rotationOffset, s3dKey.instance);

    	// Uncomment to try a True3D network model
    	// s3dKey.type = 0x5ad0e817;
    	// s3dKey.group = 0xbadb57f1;
    	// s3dKey.instance = 0x5dac0004;
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

        LOG_TRACE("S3D model parsed successfully: {} meshes, {} frames",
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
            LOG_TRACE("S3D thumbnail generated successfully!");
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

    if (ImGui::Begin("S3D Thumbnail", &showS3DThumbnail)) {
        // Zoom and Rotation controls
        if (s3dRenderer) {
            ImGui::Text("View Controls:");

            int previousZoom = s3dZoomLevel;
            int previousRotation = s3dRotation;

            // SC4 zoom levels: 1 (farthest) to 5 (closest)
            ImGui::SliderInt("Zoom Level", &s3dZoomLevel, 1, 5);

            // SC4 rotations: 0-3 (cardinal directions: S, E, N, W)
            const char* rotationNames[] = { "South", "East", "North", "West" };
            ImGui::SliderInt("Rotation", &s3dRotation, 0, 3);
            ImGui::SameLine();
            ImGui::Text("(%s)", rotationNames[s3dRotation]);

            // Reload S3D if zoom or rotation changed
            if (previousZoom != s3dZoomLevel || previousRotation != s3dRotation) {
                LOG_INFO("Reloading S3D with zoom={}, rotation={}", s3dZoomLevel, s3dRotation);
                s3dThumbnailGenerated = false;  // Trigger reload
                GenerateS3DThumbnail(D3D11Hook::GetDevice(), D3D11Hook::GetContext());
            }

            ImGui::Separator();
        }

        // Debug visualization mode selector
        if (s3dRenderer) {
            ImGui::Text("Debug mode:");

            static int currentDebugMode = 0; // 0=Normal, 1=Wireframe, etc.
            const char* debugModeNames[] = {
                "Normal",
                "Wireframe",
                "UV Coordinates",
                "Vertex Color Only",
                "Material ID",
                "Texture Only",
                "Alpha Test Visualization"
            };

            int previousMode = currentDebugMode;
            if (ImGui::Combo("##DebugMode", &currentDebugMode, debugModeNames, IM_ARRAYSIZE(debugModeNames))) {
                // Mode changed, update renderer and regenerate thumbnail
                s3dRenderer->SetDebugMode(static_cast<S3D::DebugMode>(currentDebugMode));

                // Release old thumbnail and regenerate with new debug mode
                if (s3dThumbnailSRV) {
                    s3dThumbnailSRV->Release();
                    s3dThumbnailSRV = nullptr;
                }

                s3dThumbnailSRV = s3dRenderer->GenerateThumbnail(256);
                if (s3dThumbnailSRV) {
                    LOG_INFO("Regenerated S3D thumbnail with debug mode: {}", debugModeNames[currentDebugMode]);
                } else {
                    LOG_ERROR("Failed to regenerate S3D thumbnail");
                }
            }

            // Show description for current mode
            ImGui::TextWrapped("Description:");
            switch (currentDebugMode) {
                case 0: ImGui::TextWrapped("Normal rendering with textures and materials"); break;
                case 1: ImGui::TextWrapped("Wireframe overlay to see mesh topology"); break;
                case 2: ImGui::TextWrapped("UV coordinates as colors (Red=U, Green=V)"); break;
                case 3: ImGui::TextWrapped("Vertex colors only (no textures)"); break;
                case 4: ImGui::TextWrapped("Unique color per material ID"); break;
                case 5: ImGui::TextWrapped("Normals visualization (not available - no normal data)"); break;
                case 6: ImGui::TextWrapped("Textures without vertex color modulation"); break;
                case 7: ImGui::TextWrapped("Alpha test visualization (Green=kept, Red=discarded)"); break;
            }

            ImGui::Separator();
        }

        if (s3dThumbnailSRV) {
            ImGui::Text("Thumbnail generated:");
            ImGui::Image(s3dThumbnailSRV, ImVec2(256, 256));
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
