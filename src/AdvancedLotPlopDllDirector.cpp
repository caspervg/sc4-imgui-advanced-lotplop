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

#include "cISC4LotConfiguration.h"
#include "cISC4LotConfigurationManager.h"
#include "utils/D3D11Hook.h"

class AdvancedLotPlopDllDirector;
static constexpr uint32_t kMessageCheatIssued = 0x230E27AC;
static constexpr uint32_t kSC4MessagePostCityInit = 0x26D31EC1;
static constexpr uint32_t kSC4MessagePreCityShutdown = 0x26D31EC2;

static constexpr uint32_t kAdvancedLotPlopDirectorID = 0xF78115BE;	// Randomly generated ID to avoid conflicts with other mods

static constexpr uint32_t kLotPlopCheatID = 0x4AC096C6;

struct LotConfigEntry {
	uint32_t id;
	std::string name;
	uint32_t sizeX, sizeZ;
	uint16_t minCapacity, maxCapacity;
	uint8_t growthStage;
};

AdvancedLotPlopDllDirector* GetLotPlopDirector();

class AdvancedLotPlopDllDirector final : public cRZMessage2COMDirector
{
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
	      selectedLotIID(0)
	{
		Logger::Initialize("SC4AdvancedLotPlop");
		LOG_INFO("SC4AdvancedLotPlop v{}", PLUGIN_VERSION_STR);

		searchBuffer[0] = '\0';
	}

	~AdvancedLotPlopDllDirector() override {
		LOG_INFO("~AdvancedLotPlopDllDirector()");

		if (mImGuiInitialized) {
			ImGui_ImplDX11_Shutdown();
			ImGui_ImplWin32_Shutdown();
			ImGui::DestroyContext();
			mImGuiInitialized = false;
		}
		D3D11Hook::Shutdown();

		Logger::Shutdown();
	}

	[[nodiscard]] uint32_t GetDirectorID() const override
	{
		return kAdvancedLotPlopDirectorID;
	}

	bool OnStart(cIGZCOM* pCOM) override
	{
		mpFrameWork->AddHook(this);
		return true;
	}

	void PostCityInit(cIGZMessage2Standard* pStandardMsg)
	{
		cISC4AppPtr pSC4App;
		cIGZMessageServer2Ptr pMessageServer;
		cIGZApp* pApp = mpFrameWork->Application();

		pCity = static_cast<cISC4City*>(pStandardMsg->GetVoid1());

		// Get the game window using Windows API - simpler and more reliable
		if (!mImGuiInitialized) {
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
					ImGui_ImplWin32_Init(hGameWindow);             // Win32 backend now; DX11 renderer later
					mImGuiInitialized = true;
				} else {
					LOG_WARN("D3D11Hook failed - ImGui will not be available");
					ImGui::DestroyContext();
				}
			} else {
				LOG_ERROR("Failed to find SimCity 4 window");
			}
		}
	}

	bool PreAppInit() override {
		return true;
	}

	bool PostAppInit() override
	{
		cIGZMessageServer2Ptr pMS2;
		LOG_INFO("PostAppInit: Initializing AdvancedLotPlopDllDirector");

		cIGZApp* const pApp = mpFrameWork->Application();

		if (pApp)
		{
			cRZAutoRefCount<cISC4App> pSC4App;

			if (pApp->QueryInterface(GZIID_cISC4App, pSC4App.AsPPVoid()))
			{
				pCheatCodeManager = pSC4App->GetCheatCodeManager();
			}
		}

		if (pMS2)
		{
			pMS2->AddNotification(this, kSC4MessagePostCityInit);
			pMS2->AddNotification(this, kSC4MessagePreCityShutdown);
			this->pMS2 = pMS2;
		}

		return true;
	}

	void PreCityShutdown(cIGZMessage2Standard* pStandardMsg)
	{
		cISC4View3DWin* localView3D = pView3D;
		pView3D = nullptr;

		if (localView3D)
		{
			localView3D->Release();
		}
	}

	void RenderUI() {
		if (!showWindow) return;

		ImGui::SetNextWindowSize(ImVec2(700, 600), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Lot Plop Selector", &showWindow)) {
			RenderFilters();
			ImGui::Separator();
			RenderLotList();
			ImGui::Separator();
			RenderDetails();
		}
		ImGui::End();
	}

	void ToggleWindow() {
		showWindow = !showWindow;
		if (showWindow) {
			RefreshLotList();
		}
	}

private:
	cIGZCheatCodeManager* pCheatCodeManager;
	cISC4City* pCity;
	cISC4View3DWin* pView3D;
	cIGZMessageServer2* pMS2;
	bool showWindow;
	uint8_t filterZoneType;
	uint8_t filterWealthType;
	uint32_t minSizeX, maxSizeX;
	uint32_t minSizeZ, maxSizeZ;
	char searchBuffer[256];
	std::vector<LotConfigEntry> lotEntries;
	uint32_t selectedLotIID;
	bool mImGuiInitialized = false;

	void RenderFilters() {
		ImGui::Text("Filters");

		// Zone Type
		const char* zoneTypes[] = { "Any", "Residential", "Commercial", "Industrial", "Agriculture" };
		int currentZone = (filterZoneType == 0xFF) ? 0 : (filterZoneType + 1);
		if (ImGui::Combo("Zone Type", &currentZone, zoneTypes, IM_ARRAYSIZE(zoneTypes))) {
			filterZoneType = (currentZone == 0) ? 0xFF : (currentZone - 1);
			RefreshLotList();
		}

		// Wealth Type
		const char* wealthTypes[] = { "Any", "Low ($)", "Medium ($$)", "High ($$$)" };
		int currentWealth = (filterWealthType == 0xFF) ? 0 : (filterWealthType + 1);
		if (ImGui::Combo("Wealth", &currentWealth, wealthTypes, IM_ARRAYSIZE(wealthTypes))) {
			filterWealthType = (currentWealth == 0) ? 0xFF : (currentWealth - 1);
			RefreshLotList();
		}

		// Size sliders
		ImGui::Text("Size Range:");
		if (ImGui::SliderInt("Min Width", (int*)&minSizeX, 1, 16) |
			ImGui::SliderInt("Max Width", (int*)&maxSizeX, 1, 16) |
			ImGui::SliderInt("Min Depth", (int*)&minSizeZ, 1, 16) |
			ImGui::SliderInt("Max Depth", (int*)&maxSizeZ, 1, 16)) {
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

		if (ImGui::BeginTable("LotTable", 5,
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {

			ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 80);
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 60);
			ImGui::TableSetupColumn("Capacity", ImGuiTableColumnFlags_WidthFixed, 80);
			ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthFixed, 50);
			ImGui::TableHeadersRow();

			for (const auto& entry : lotEntries) {
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

				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%u-%u", entry.minCapacity, entry.maxCapacity);

				ImGui::TableSetColumnIndex(4);
				ImGui::Text("%u", entry.growthStage);
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
			[this](const LotConfigEntry& e) { return e.id == selectedLotIID; });

		if (it != lotEntries.end()) {
			ImGui::Text("Selected Lot: %s", it->name.c_str());
			ImGui::Text("ID: 0x%08X", it->id);
			ImGui::Text("Size: %ux%u", it->sizeX, it->sizeZ);

			ImGui::Spacing();
			if (ImGui::Button("Plop This Lot (Uses game's plop tool)")) {
				TriggerLotPlop(selectedLotIID);
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Activates the game's built-in plop tool.\nClick in the city to place the building.");
			}
		}
	}

	void RefreshLotList() {
		lotEntries.clear();

		cISC4LotConfigurationManager* pLotConfigMgr = pCity->GetLotConfigurationManager();
		if (!pLotConfigMgr) return;

		LOG_INFO("Got LotConfigurationManager");

		for (uint32_t x = minSizeX; x <= maxSizeX; x++) {
			for (uint32_t z = minSizeZ; z <= maxSizeZ; z++) {
				std::unordered_set<uint32_t> configIDs;  // Use IDs, not pointers

				if (pLotConfigMgr->GetLotConfigurationIDsBySize(configIDs, x, z)) {
					// Now iterate the IDs and fetch configs individually
					for (uint32_t id : configIDs) {
						cISC4LotConfiguration* pConfig = pLotConfigMgr->GetLotConfiguration(id);
						if (!pConfig) continue;

						if (!MatchesFilters(pConfig)) continue;

						LotConfigEntry entry;
						entry.id = id;

						cRZBaseString name;
						if (pConfig->GetName(name)) {
							entry.name = name.Data();
						}

						pConfig->GetSize(entry.sizeX, entry.sizeZ);
						entry.minCapacity = pConfig->GetMinBuildingCapacity();
						entry.maxCapacity = pConfig->GetMaxBuildingCapacity();
						entry.growthStage = pConfig->GetGrowthStage();

						// // Search filter
						// if (searchBuffer[0] != '\0') {
						//     std::string searchLower = searchBuffer;
						//     std::transform(searchLower.begin(), searchLower.end(),
						//                  searchLower.begin(), ::tolower);
						//     std::string nameLower = entry.name;
						//     std::transform(nameLower.begin(), nameLower.end(),
						//                  nameLower.begin(), ::tolower);
						//
						//     if (nameLower.find(searchLower) == std::string::npos) {
						//         continue;
						//     }
						// }

						lotEntries.push_back(entry);
					}
				}
			}
		}
	}

	bool MatchesFilters(cISC4LotConfiguration* pConfig) {
		if (filterZoneType != 0xFF) {
			cISC4ZoneManager::ZoneType zoneType = static_cast<cISC4ZoneManager::ZoneType>(filterZoneType);
			if (!pConfig->IsCompatibleWithZoneType(zoneType)) return false;
		}

		if (filterWealthType != 0xFF) {
			cISC4BuildingOccupant::WealthType wealthType =
				static_cast<cISC4BuildingOccupant::WealthType>(filterWealthType);
			if (!pConfig->IsCompatibleWithWealthType(wealthType)) return false;
		}

		return true;
	}

	void ClearFilters() {
		filterZoneType = 0xFF;
		filterWealthType = 0xFF;
		minSizeX = 1; maxSizeX = 16;
		minSizeZ = 1; maxSizeZ = 16;
		searchBuffer[0] = '\0';
	}

	void TriggerLotPlop(uint32_t lotID) {
		// Format: "lotplop 0x12345678"
		char cheatString[64];
		snprintf(cheatString, sizeof(cheatString), "lotplop 0x%08X", lotID);

		cRZBaseString cheatStr;
		cheatStr.FromChar(cheatString);

		uint32_t cheatID = kLotPlopCheatID;
		if (pCheatCodeManager->DoesCheatCodeMatch(cheatStr, cheatID)) {
			// Send the cheat notification - this activates the game's plop tool
			pCheatCodeManager->SendCheatNotifications(cheatStr, cheatID);
		}
	}

	bool DoMessage(cIGZMessage2* pMsg)
	{
		auto pStandardMsg = static_cast<cIGZMessage2Standard*>(pMsg);

		switch (pMsg->GetType())
		{
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
		ID3D11Device* pDevice,
		ID3D11DeviceContext* pContext,
		IDXGISwapChain* pSwapChain)
	{
		static bool initialized = false;
		static ID3D11RenderTargetView* pRTV = nullptr;

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
			ID3D11Texture2D* pBackBuffer = nullptr;
			HRESULT hr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pBackBuffer));
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

		// Example UI
		//ImGui::ShowDemoWindow();

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

cRZCOMDllDirector* RZGetCOMDllDirector() {
	return &sDirector;
}

AdvancedLotPlopDllDirector* GetLotPlopDirector() {
	return &sDirector;
}