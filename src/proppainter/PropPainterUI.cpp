#include "PropPainterUI.h"
#include "imgui.h"
#include "../utils/Logger.h"
#include <cstring>

PropPainterUI::PropPainterUI()
    : showWindow(false)
    , showLoadingWindow(false)
    , paintingActive(false)
    , pCacheManager(nullptr)
    , loadingCurrent(0)
    , loadingTotal(0)
    , selectedPropID(0)
    , selectedRotation(0)
    , thumbnailSize(64)
    , gridSpacing(8.0f)
{
    searchBuffer[0] = '\0';
}

PropPainterUI::~PropPainterUI() {
}

void PropPainterUI::Render() {
    RenderLoadingWindow();

    if (!showWindow) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(900, 700), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Prop Painter", &showWindow)) {
        // Check if cache is ready
        if (!pCacheManager || !pCacheManager->IsInitialized()) {
            ImGui::TextWrapped("Prop cache not initialized. Please wait...");
            if (ImGui::Button("Build Cache")) {
                if (callbacks.OnBuildCache) {
                    callbacks.OnBuildCache();
                }
            }
        } else {
            // Top toolbar/filters section
            RenderToolbar();
            ImGui::Separator();

            // Main two-column layout
            float leftWidth = 600.0f;
            float rightWidth = ImGui::GetContentRegionAvail().x - leftWidth - ImGui::GetStyle().ItemSpacing.x;

            ImGui::BeginChild("LeftPanel", ImVec2(leftWidth, 0), false);
            {
                // Prop browser takes most of left panel
                float browserHeight = ImGui::GetContentRegionAvail().y - 180;
                ImGui::BeginChild("PropBrowser", ImVec2(0, browserHeight), true);
                RenderPropBrowser();
                ImGui::EndChild();

                ImGui::Spacing();

                // Preview at bottom of left panel
                ImGui::BeginChild("PropPreview", ImVec2(0, 0), true);
                RenderPropPreview();
                ImGui::EndChild();
            }
            ImGui::EndChild();

            ImGui::SameLine();

            ImGui::BeginChild("RightPanel", ImVec2(rightWidth, 0), false);
            {
                // Controls at top
                ImGui::BeginChild("Controls", ImVec2(0, 200), true);
                RenderPaintingControls();
                ImGui::EndChild();

                ImGui::Spacing();

                // Details below
                ImGui::BeginChild("Details", ImVec2(0, 0), true);
                RenderPropDetails();
                ImGui::EndChild();
            }
            ImGui::EndChild();
        }
    }
    ImGui::End();
}

void PropPainterUI::RenderLoadingWindow() {
    if (!showLoadingWindow) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(400, 150), ImGuiCond_Always);
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("Building prop cache", nullptr, flags)) {
        ImGui::TextWrapped("Building prop cache, please wait...");
        ImGui::Spacing();

        if (loadingTotal > 0) {
            float progress = static_cast<float>(loadingCurrent) / static_cast<float>(loadingTotal);
            ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f));
            ImGui::Text("%s (%d / %d)", loadingStage.c_str(), loadingCurrent, loadingTotal);
        } else {
            ImGui::TextWrapped("Initializing...");
        }
    }
    ImGui::End();
}

void PropPainterUI::RenderToolbar() {
    // Search and quick actions in toolbar
    ImGui::PushItemWidth(300);
    ImGui::InputText("##Search", searchBuffer, sizeof(searchBuffer));
    ImGui::PopItemWidth();

    ImGui::SameLine();
    if (ImGui::Button("Clear Search")) {
        searchBuffer[0] = '\0';
    }

    ImGui::SameLine();
    ImGui::Dummy(ImVec2(20, 0));

    ImGui::SameLine();
    if (ImGui::Button("Refresh Cache")) {
        if (callbacks.OnBuildCache) {
            callbacks.OnBuildCache();
        }
    }
}

void PropPainterUI::RenderPropPreview() {
    if (selectedPropID == 0 || !pCacheManager) {
        ImGui::TextWrapped("No prop selected");
        return;
    }

    const PropCacheEntry* entry = pCacheManager->GetPropByID(selectedPropID);
    if (!entry || !entry->iconSRV) {
        ImGui::TextWrapped("No preview available");
        return;
    }

    float availWidth = ImGui::GetContentRegionAvail().x;
    float previewSize = availWidth - 20;
    if (previewSize > 150) previewSize = 150;

    ImVec2 cursorPos = ImGui::GetCursorPos();
    float offset = (availWidth - previewSize) / 2.0f;
    ImGui::SetCursorPos(ImVec2(cursorPos.x + offset, cursorPos.y));

    ImGui::Image(entry->iconSRV, ImVec2(previewSize, previewSize));
}

void PropPainterUI::RenderPropBrowser() {
    if (!pCacheManager) {
        return;
    }

    const auto& allProps = pCacheManager->GetAllProps();

    // Count filtered props
    size_t filteredCount = 0;
    for (const auto& prop : allProps) {
        if (searchBuffer[0] != '\0') {
            if (prop.name.find(searchBuffer) == std::string::npos) {
                continue;
            }
        }
        filteredCount++;
    }

    ImGui::Text("Total: %zu | Showing: %zu", allProps.size(), filteredCount);
    ImGui::Separator();

    // Render props table
    if (ImGui::BeginTable("PropTable", 3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable,
                          ImVec2(0.0f, 0.0f)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 56.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort);
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableHeadersRow();

        // Build filtered indices
        std::vector<int> filteredIndices;
        for (size_t i = 0; i < allProps.size(); ++i) {
            const auto& prop = allProps[i];
            if (searchBuffer[0] != '\0') {
                if (prop.name.find(searchBuffer) == std::string::npos) {
                    continue;
                }
            }
            filteredIndices.push_back(static_cast<int>(i));
        }

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(filteredIndices.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                int idx = filteredIndices[row];
                const auto& prop = allProps[idx];

                ImGui::TableNextRow();
                ImGui::PushID(idx);

                // Icon column
                ImGui::TableSetColumnIndex(0);
                if (prop.iconSRV) {
                    float displaySize = 44.0f;
                    ImVec2 cursorPos = ImGui::GetCursorPos();

                    if (prop.iconWidth < 44) {
                        float offset = (44.0f - prop.iconWidth) / 2.0f;
                        ImGui::SetCursorPos(ImVec2(cursorPos.x + offset, cursorPos.y + offset));
                        displaySize = static_cast<float>(prop.iconWidth);
                    }

                    ImGui::Image(prop.iconSRV, ImVec2(displaySize, displaySize));
                } else {
                    ImGui::Dummy(ImVec2(44, 44));
                }

                // Name column
                ImGui::TableSetColumnIndex(1);
                bool isSelected = (prop.propID == selectedPropID);
                if (ImGui::Selectable(prop.name.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                    selectedPropID = prop.propID;
                    LOG_INFO("Selected prop: {} (ID: 0x{:08X})", prop.name, prop.propID);
                }

                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    selectedPropID = prop.propID;
                    if (!paintingActive && callbacks.OnStartPainting) {
                        paintingActive = true;
                        callbacks.OnStartPainting(selectedPropID, selectedRotation);
                    }
                }

                // ID column
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("0x%08X", prop.propID);

                ImGui::PopID();
            }
        }

        ImGui::EndTable();
    }
}

void PropPainterUI::RenderPaintingControls() {
    ImGui::Text("Painting Controls");
    ImGui::Separator();

    // Rotation selector
    const char* rotationNames[] = { "South (0°)", "East (90°)", "North (180°)", "West (270°)" };
    ImGui::Text("Rotation:");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##Rotation", &selectedRotation, rotationNames, 4)) {
        // Update rotation in painting mode if active
        if (paintingActive && callbacks.OnStartPainting) {
            callbacks.OnStartPainting(selectedPropID, selectedRotation);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Painting toggle
    if (!paintingActive) {
        ImGui::BeginDisabled(selectedPropID == 0);
        if (ImGui::Button("Start Painting", ImVec2(-1, 50))) {
            paintingActive = true;
            if (callbacks.OnStartPainting) {
                callbacks.OnStartPainting(selectedPropID, selectedRotation);
            }
            LOG_INFO("Started painting mode for prop 0x{:08X}", selectedPropID);
        }
        ImGui::EndDisabled();

        // if (selectedPropID == 0) {
        //     ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Select a prop to begin");
        // }
    } else {
        if (ImGui::Button("Stop Painting", ImVec2(-1, 50))) {
            paintingActive = false;
            if (callbacks.OnStopPainting) {
                callbacks.OnStopPainting();
            }
            LOG_INFO("Stopped painting mode");
        }

        // ImGui::Spacing();
        // ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "● Painting Active");
        // ImGui::TextWrapped("Click in the city to place props");
    }
}

void PropPainterUI::RenderPropDetails() {
    ImGui::Text("Prop Information");
    ImGui::Separator();

    if (selectedPropID == 0 || !pCacheManager) {
        ImGui::TextWrapped("No prop selected");
        return;
    }

    const PropCacheEntry* entry = pCacheManager->GetPropByID(selectedPropID);
    if (!entry) {
        ImGui::TextWrapped("Prop not found in cache");
        return;
    }

    ImGui::Text("Name:");
    ImGui::Indent();
    ImGui::TextWrapped("%s", entry->name.c_str());
    ImGui::Unindent();

    ImGui::Spacing();
    ImGui::Text("Exemplar IID:");
    ImGui::Indent();
    ImGui::Text("0x%08X", entry->exemplarIID);
    ImGui::Unindent();

    if (entry->s3dType != 0) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("S3D Resource:");
        ImGui::Indent();
        ImGui::Text("Type:     0x%08X", entry->s3dType);
        ImGui::Text("Group:    0x%08X", entry->s3dGroup);
        ImGui::Text("Instance: 0x%08X", entry->s3dInstance);
        ImGui::Unindent();
    }
}

void PropPainterUI::UpdateLoadingProgress(const char* stage, int current, int total) {
    loadingStage = stage;
    loadingCurrent = current;
    loadingTotal = total;
}
