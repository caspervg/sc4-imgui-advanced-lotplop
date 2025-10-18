#include "AdvancedLotPlopUI.h"
#include "../vendor/imgui/imgui.h"
#include "LotConfigEntry.h"
#include "../utils/Config.h"

#include <algorithm>
#include <unordered_set>

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

    // Occupant Group multiselect with tree groups based on prefix before ':' in the display name
    if (ImGui::CollapsingHeader("Occupant Groups")) {
        RenderOccupantGroupFilter();
    }

    if (ImGui::Button("Clear Filters")) {
        filterZoneType = 0xFF;
        filterWealthType = 0xFF;
        minSizeX = 1; maxSizeX = 16;
        minSizeZ = 1; maxSizeZ = 16;
        searchBuffer[0] = '\0';
        selectedOccupantGroups.clear();
        if (callbacks.OnRefreshList) callbacks.OnRefreshList();
    }
}

void AdvancedLotPlopUI::RenderOccupantGroupFilter() {
    const auto& names = Config::GetOccupantGroupNames();

    // Build a set for quick lookup and change tracking
    std::unordered_set<uint32_t> selectedSet(selectedOccupantGroups.begin(), selectedOccupantGroups.end());
    bool anyChanged = false;

    // Helpers: trim and split prefix before ':'
    auto trim = [](const std::string& s) -> std::string {
        size_t a = s.find_first_not_of(" \t\r\n");
        size_t b = s.find_last_not_of(" \t\r\n");
        if (a == std::string::npos) return std::string();
        return s.substr(a, b - a + 1);
    };
    auto extract_group = [&](const std::string& full)->std::pair<std::string,std::string> {
        size_t pos = full.find(':');
        if (pos == std::string::npos) {
            return {"Other", trim(full)}; // No prefix -> Other
        }
        std::string left = trim(full.substr(0, pos));
        std::string right = trim(full.substr(pos + 1));
        if (left.empty()) left = "Other";
        return {left, right.empty() ? trim(full) : right};
    };

    // Build grouping: GroupLabel -> vector of (id, full_label, short_label)
    struct Item { uint32_t id; std::string full; std::string short_name; };
    std::unordered_map<std::string, std::vector<Item>> groups;
    groups.reserve(names.size());
    for (const auto& kv : names) {
        uint32_t id = kv.first;
        const std::string& display = kv.second;
        auto pr = extract_group(display);
        groups[pr.first].push_back(Item{id, display, pr.second});
    }

    // Prepare group ordering by the minimum OG id inside each group
    struct GroupRow { std::string label; uint32_t min_id; };
    std::vector<GroupRow> groupRows;
    groupRows.reserve(groups.size());
    for (auto& gkv : groups) {
        auto& vec = gkv.second;
        std::sort(vec.begin(), vec.end(), [](const Item& a, const Item& b){ return a.id < b.id; });
        uint32_t min_id = vec.empty() ? 0xFFFFFFFFu : vec.front().id;
        groupRows.push_back(GroupRow{gkv.first, min_id});
    }
    std::sort(groupRows.begin(), groupRows.end(), [](const GroupRow& a, const GroupRow& b){ return a.min_id < b.min_id; });

    // Render groups collapsed by default
    for (const auto& row : groupRows) {
        auto& vec = groups[row.label];
        ImGuiTreeNodeFlags flags = 0; // collapsed by default
        if (ImGui::TreeNodeEx(row.label.c_str(), flags)) {
            for (const auto& it : vec) {
                bool checked = selectedSet.count(it.id) != 0;
                char buf[256];
                const std::string& base = it.short_name.empty() ? it.full : it.short_name;
                snprintf(buf, sizeof(buf), "%s (0x%08X)", base.c_str(), it.id);
                if (ImGui::Checkbox(buf, &checked)) {
                    anyChanged = true;
                    if (checked) selectedSet.insert(it.id); else selectedSet.erase(it.id);
                }
            }
            ImGui::TreePop();
        }
    }

    if (anyChanged) {
        selectedOccupantGroups.assign(selectedSet.begin(), selectedSet.end());
        if (callbacks.OnRefreshList) callbacks.OnRefreshList();
    }
    if (ImGui::Button("Clear Group Selection")) {
        selectedOccupantGroups.clear();
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
                if (!entry.description.empty() && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", entry.description.c_str());
                }

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

        if (!it->description.empty()) {
            ImGui::Separator();
            ImGui::TextWrapped("%s", it->description.c_str());
        }

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
