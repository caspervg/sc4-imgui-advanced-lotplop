#pragma once

#include <vector>
#include <string>
#include <cstdint>
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

    // Data hooks
    void SetCity(cISC4City* city);

    // Data shared from director
    void SetLotEntries(const std::vector<LotConfigEntry>* entries);

    // Window flag exposed for director to toggle
    bool* GetShowWindowPtr();

    // Selected lot iid
    uint32_t GetSelectedLotIID() const;
    void SetSelectedLotIID(uint32_t iid);

    // Filters access
    uint8_t GetFilterZoneType() const;
    uint8_t GetFilterWealthType() const;
    uint32_t GetMinSizeX() const; uint32_t GetMaxSizeX() const;
    uint32_t GetMinSizeZ() const; uint32_t GetMaxSizeZ() const;
    const char* GetSearchBuffer() const;
    const std::vector<uint32_t>& GetSelectedOccupantGroups() const { return selectedOccupantGroups; }

    // Render entrypoint (assumes ImGui context is active)
    void Render();

    // Mutators used by director to sync from saved state if needed
    void SetFilters(uint8_t zone, uint8_t wealth, uint32_t minX, uint32_t maxX, uint32_t minZ, uint32_t maxZ, const char* search);

private:
    void RenderFilters();
    void RenderLotList();
    void RenderDetails();
    void RenderOccupantGroupFilter();

    AdvancedLotPlopUICallbacks callbacks{};

    cISC4City* pCity = nullptr;

    // UI state
    bool showWindow = true;
    uint8_t filterZoneType = 0xFF; // 0xFF means Any
    uint8_t filterWealthType = 0xFF; // 0xFF means Any
    uint32_t minSizeX = 1, maxSizeX = 16;
    uint32_t minSizeZ = 1, maxSizeZ = 16;
    char searchBuffer[256]{};

    const std::vector<LotConfigEntry>* lotEntries = nullptr; // not owned

    uint32_t selectedLotIID = 0;

    // Advanced filters
    std::vector<uint32_t> selectedOccupantGroups; // chosen in UI
};
