#include "AdvancedLotPlopUI.h"
#include "../vendor/imgui/imgui.h"
#include "LotConfigEntry.h"

#include <algorithm>

AdvancedLotPlopUI::AdvancedLotPlopUI() {
    searchBuffer[0] = '\0';
}

void AdvancedLotPlopUI::SetCallbacks(const AdvancedLotPlopUICallbacks &cb) {
    callbacks = cb;
}

void AdvancedLotPlopUI::SetCity(cISC4City *city) {
    pCity = city;
}

void AdvancedLotPlopUI::SetLotEntries(const std::vector<LotConfigEntry> *entries) {
    lotEntries = entries;
}

bool *AdvancedLotPlopUI::GetShowWindowPtr() {
    return &showWindow;
}

uint32_t AdvancedLotPlopUI::GetSelectedLotIID() const { return selectedLotIID; }
void AdvancedLotPlopUI::SetSelectedLotIID(uint32_t iid) { selectedLotIID = iid; }

uint8_t AdvancedLotPlopUI::GetFilterZoneType() const { return filterZoneType; }
uint8_t AdvancedLotPlopUI::GetFilterWealthType() const { return filterWealthType; }
uint32_t AdvancedLotPlopUI::GetMinSizeX() const { return minSizeX; }
uint32_t AdvancedLotPlopUI::GetMaxSizeX() const { return maxSizeX; }
uint32_t AdvancedLotPlopUI::GetMinSizeZ() const { return minSizeZ; }
uint32_t AdvancedLotPlopUI::GetMaxSizeZ() const { return maxSizeZ; }
const char *AdvancedLotPlopUI::GetSearchBuffer() const { return searchBuffer; }

void AdvancedLotPlopUI::SetFilters(uint8_t zone, uint8_t wealth, uint32_t minX, uint32_t maxX, uint32_t minZ, uint32_t maxZ, const char *search) {
    filterZoneType = zone; filterWealthType = wealth;
    minSizeX = minX; maxSizeX = maxX; minSizeZ = minZ; maxSizeZ = maxZ;
    if (search) {
        strncpy_s(searchBuffer, search, sizeof(searchBuffer) - 1);
        searchBuffer[sizeof(searchBuffer) - 1] = '\0';
    }
}

void AdvancedLotPlopUI::Render() {
    if (!showWindow) return;

    ImGui::SetNextWindowSize(ImVec2(700, 600), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Advanced LotPlop", &showWindow)) {
        RenderFilters();
        ImGui::Separator();
        RenderLotList();
        ImGui::Separator();
        RenderDetails();
    }
    ImGui::End();
}

void AdvancedLotPlopUI::RenderFilters() {
    ImGui::Text("Filters");

    // Zone Type labels based on SC4 zone categories
    const char *zoneTypes[] = {"Any", "Residential (R)", "Commercial (C)", "Industrial (I)", "Agriculture", "Plopped", "None", "Other"};
    int currentZone = (filterZoneType == 0xFF) ? 0 : (filterZoneType + 1);
    if (ImGui::Combo("Zone Type", &currentZone, zoneTypes, IM_ARRAYSIZE(zoneTypes))) {
        // Store as category index: 0=R,1=C,2=I,3=Agriculture,4=Plopped,5=None,6=Other; 0xFF means Any
        filterZoneType = (currentZone == 0) ? 0xFF : static_cast<uint8_t>(currentZone - 1);
        if (callbacks.OnRefreshList) callbacks.OnRefreshList();
    }

    // Wealth Type per SC4: $, $$, $$$
    const char *wealthTypes[] = {"Any", "Low ($)", "Medium ($$)", "High ($$$)"};
    int currentWealth = (filterWealthType == 0xFF) ? 0 : (filterWealthType + 1);
    if (ImGui::Combo("Wealth", &currentWealth, wealthTypes, IM_ARRAYSIZE(wealthTypes))) {
        filterWealthType = (currentWealth == 0) ? 0xFF : (currentWealth - 1);
        if (callbacks.OnRefreshList) callbacks.OnRefreshList();
    }

    // Size sliders
    ImGui::Text("Size Range:");
    if (ImGui::SliderInt("Min Width", (int *) &minSizeX, 1, 16) |
        ImGui::SliderInt("Max Width", (int *) &maxSizeX, 1, 16) |
        ImGui::SliderInt("Min Depth", (int *) &minSizeZ, 1, 16) |
        ImGui::SliderInt("Max Depth", (int *) &maxSizeZ, 1, 16)) {
        if (callbacks.OnRefreshList) callbacks.OnRefreshList();
    }

    // Search
    if (ImGui::InputText("Search", searchBuffer, sizeof(searchBuffer))) {
        if (callbacks.OnRefreshList) callbacks.OnRefreshList();
    }

    if (ImGui::Button("Clear Filters")) {
        filterZoneType = 0xFF;
        filterWealthType = 0xFF;
        minSizeX = 1; maxSizeX = 16;
        minSizeZ = 1; maxSizeZ = 16;
        searchBuffer[0] = '\0';
        if (callbacks.OnRefreshList) callbacks.OnRefreshList();
    }
}

void AdvancedLotPlopUI::RenderLotList() {
    size_t count = lotEntries ? lotEntries->size() : 0;
    ImGui::Text("Lot Configurations (%zu found)", count);

    if (ImGui::BeginTable("LotTable", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed, 56.0f);
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableHeadersRow();

        if (lotEntries) {
            for (const auto &entry: *lotEntries) {
                ImGui::TableNextRow();

                // Icon column
                ImGui::TableSetColumnIndex(0);
                if (entry.iconSRV && entry.iconWidth > 0 && entry.iconHeight > 0) {
                    // Lot PNG icons are 176x44 made of four 44x44 states; show the second 44x44 (enabled) [pixels 44..88]
                    float u1 = (entry.iconWidth > 0) ? (44.0f / (float)entry.iconWidth) : 0.0f;
                    float v1 = 0.0f;
                    float u2 = (entry.iconWidth > 0) ? (88.0f / (float)entry.iconWidth) : 0.0f;
                    float v2 = (entry.iconHeight > 0) ? (44.0f / (float)entry.iconHeight) : 0.0f;
                    ImGui::Image((ImTextureID)entry.iconSRV, ImVec2(44, 44), ImVec2(u1, v1), ImVec2(u2, v2));
                } else {
                    ImGui::Dummy(ImVec2(44, 44));
                }

                // ID column + selection behavior spanning the row
                ImGui::TableSetColumnIndex(1);
                bool isSelected = (entry.id == selectedLotIID);
                char label[32];
                snprintf(label, sizeof(label), "0x%08X", entry.id);
                if (ImGui::Selectable(label, isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                    selectedLotIID = entry.id;
                }

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%s", entry.name.c_str());

                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%ux%u", entry.sizeX, entry.sizeZ);
            }
        }

        ImGui::EndTable();
    }
}

void AdvancedLotPlopUI::RenderDetails() {
    if (selectedLotIID == 0) {
        ImGui::Text("No lot selected");
        return;
    }

    if (!lotEntries) return;

    auto it = std::find_if(lotEntries->begin(), lotEntries->end(),
                           [this](const LotConfigEntry &e) { return e.id == selectedLotIID; });

    if (it != lotEntries->end()) {
        ImGui::Text("Selected Lot: %s", it->name.c_str());
        ImGui::Text("ID: 0x%08X", it->id);
        ImGui::Text("Size: %ux%u", it->sizeX, it->sizeZ);

        ImGui::Spacing();
        if (ImGui::Button("Plop")) {
            if (callbacks.OnPlop) callbacks.OnPlop(selectedLotIID);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Activates the game's built-in plop tool.\nClick in the city to place the building.");
        }
    }
}
