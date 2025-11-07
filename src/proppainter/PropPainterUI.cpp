#include "PropPainterUI.h"
#include "PropPainterInputControl.h"
#include "CoordinateConverter.h"
#include "imgui.h"
#include "cISC43DRender.h"
#include "../utils/Logger.h"
#include <cstring>

PropPainterUI::PropPainterUI()
    : showWindow(false)
    , showLoadingWindow(false)
    , paintingActive(false)
    , pCacheManager(nullptr)
    , pInputControl(nullptr)
    , pRenderer(nullptr)
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

void PropPainterUI::RenderPreviewOverlay() {
    if (!paintingActive || !pInputControl || !pRenderer) {
        return;
    }

    const PropPainterPreviewState& preview = pInputControl->GetPreviewState();
    if (!preview.cursorValid) {
        return;
    }

    // Convert world position to screen coordinates
    float screenX, screenY;
    if (!CoordinateConverter::WorldToScreen(pRenderer, preview.cursorWorldPos, screenX, screenY)) {
        return;  // Position not visible on screen
    }

    ImDrawList* drawList = ImGui::GetForegroundDrawList();

    // Draw crosshair at cursor position
    const float crosshairSize = 20.0f;
    const ImU32 crosshairColor = IM_COL32(0, 255, 0, 200);  // Green, semi-transparent
    const float thickness = 2.0f;

    ImVec2 center(screenX, screenY);

    // Horizontal line
    drawList->AddLine(
        ImVec2(center.x - crosshairSize, center.y),
        ImVec2(center.x + crosshairSize, center.y),
        crosshairColor, thickness);

    // Vertical line
    drawList->AddLine(
        ImVec2(center.x, center.y - crosshairSize),
        ImVec2(center.x, center.y + crosshairSize),
        crosshairColor, thickness);

    // Draw circle around cursor
    drawList->AddCircle(center, crosshairSize, crosshairColor, 32, thickness);

    // Draw info box near cursor
    const float offsetX = 30.0f;
    const float offsetY = -10.0f;
    ImVec2 textPos(center.x + offsetX, center.y + offsetY);

    // Background box for text
    char infoText[256];
    const char* rotationNames[] = { "South", "East", "North", "West" };
    snprintf(infoText, sizeof(infoText), "%s\nRotation: %s\n(%.1f, %.1f)",
        preview.propName.c_str(),
        rotationNames[preview.rotation % 4],
        preview.cursorWorldPos.fX,
        preview.cursorWorldPos.fZ);

    ImVec2 textSize = ImGui::CalcTextSize(infoText);
    ImVec2 boxMin(textPos.x - 4, textPos.y - 4);
    ImVec2 boxMax(textPos.x + textSize.x + 4, textPos.y + textSize.y + 4);

    // Draw semi-transparent background
    drawList->AddRectFilled(boxMin, boxMax, IM_COL32(0, 0, 0, 180), 4.0f);
    drawList->AddRect(boxMin, boxMax, IM_COL32(0, 255, 0, 255), 4.0f, 0, 1.5f);

    // Draw text
    drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), infoText);

    // TODO: Add area fill boundary rendering here when that mode is implemented
    // if (preview.isDefiningArea) { ... }
}
