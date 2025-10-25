#pragma once

#include <vector>
#include <unordered_set>
#include <unordered_map>

#include "cISC4City.h"
#include "cISC4LotConfiguration.h"

struct LotConfigEntry;

struct AdvancedLotPlopUICallbacks {
    // Called when user clicks Plop
    void (*OnPlop)(uint32_t lotID) = nullptr;
    // Rebuild cache if needed
    void (*OnBuildCache)() = nullptr;
    // Rebuild filtered list
    void (*OnRefreshList)() = nullptr;
};

class AdvancedLotPlopUI {
public:
    AdvancedLotPlopUI();
    void SetCallbacks(const AdvancedLotPlopUICallbacks& cb);
    void SetCity(cISC4City* city);
    void SetLotEntries(const std::vector<LotConfigEntry>* entries);
    bool* GetShowWindowPtr();
    uint32_t GetSelectedLotIID() const; void SetSelectedLotIID(uint32_t iid);
    uint8_t GetFilterZoneType() const; uint8_t GetFilterWealthType() const; uint32_t GetMinSizeX() const; uint32_t GetMaxSizeX() const; uint32_t GetMinSizeZ() const; uint32_t GetMaxSizeZ() const; const char* GetSearchBuffer() const; const std::vector<uint32_t>& GetSelectedOccupantGroups() const { return selectedOccupantGroups; }
    void Render();
    void ShowLoadingWindow(bool show); void SetLoadingProgress(const char* stage, int current, int total); void RenderLoadingWindow();
    void SetFilters(uint8_t zone, uint8_t wealth, uint32_t minX, uint32_t maxX, uint32_t minZ, uint32_t maxZ, const char* search);
    // Persistence
    void LoadPersistedState(); void SavePersistedState();
private:
    void RenderFilters(); void RenderLotList(); void RenderIconForEntry(const LotConfigEntry& entry); void RenderDetails(); void RenderOccupantGroupFilter(); void MarkListDirty();
    AdvancedLotPlopUICallbacks callbacks{}; cISC4City* pCity = nullptr;
    bool showWindow = false; uint8_t filterZoneType = 0xFF; uint8_t filterWealthType = 0xFF; uint32_t minSizeX = 1, maxSizeX = 16; uint32_t minSizeZ = 1, maxSizeZ = 16; char searchBuffer[256]{}; const std::vector<LotConfigEntry>* lotEntries = nullptr; uint32_t selectedLotIID = 0; std::vector<uint32_t> selectedOccupantGroups; bool showLoadingWindow = false; char loadingStage[256]{}; int loadingCurrent = 0; int loadingTotal = 0; bool listDirty = true; };
