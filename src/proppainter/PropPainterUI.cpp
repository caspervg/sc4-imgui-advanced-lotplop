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

    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
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
            // Two-column layout
            ImGui::BeginChild("PropBrowser", ImVec2(500, 0), true);
            RenderPropBrowser();
            ImGui::EndChild();

            ImGui::SameLine();

            ImGui::BeginChild("ControlPanel", ImVec2(0, 0), true);
            RenderPaintingControls();
            ImGui::Separator();
            RenderPropDetails();
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

void PropPainterUI::RenderPropBrowser() {
    ImGui::Text("Prop Browser");
    ImGui::Separator();

    // Search filter
    ImGui::InputText("Search", searchBuffer, sizeof(searchBuffer));
    ImGui::Separator();

    if (!pCacheManager) {
        return;
    }

    const auto& allProps = pCacheManager->GetAllProps();

    // Calculate grid layout
    float windowWidth = ImGui::GetContentRegionAvail().x;
    float cellSize = thumbnailSize + gridSpacing * 2;
    int columns = std::max(1, static_cast<int>(windowWidth / cellSize));

    ImGui::Text("Props: %zu", allProps.size());
    ImGui::Separator();

    // Scrollable prop grid
    if (ImGui::BeginChild("PropGrid", ImVec2(0, 0), false)) {
        int col = 0;

        for (const auto& prop : allProps) {
            // Apply search filter
            if (searchBuffer[0] != '\0') {
                if (prop.name.find(searchBuffer) == std::string::npos) {
                    continue;
                }
            }

            // Start new row
            if (col > 0) {
                ImGui::SameLine();
            }

            ImGui::BeginGroup();

            // Thumbnail button
            bool isSelected = (prop.propID == selectedPropID);
            if (isSelected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 1.0f));
            }

            ImVec2 thumbnailSizeVec(thumbnailSize, thumbnailSize);
            bool clicked = false;

            if (prop.iconSRV) {
                clicked = ImGui::ImageButton(
                    reinterpret_cast<ImTextureID>(prop.iconSRV),
                    thumbnailSizeVec
                );
            } else {
                clicked = ImGui::Button("?", thumbnailSizeVec);
            }

            if (isSelected) {
                ImGui::PopStyleColor();
            }

            if (clicked) {
                selectedPropID = prop.propID;
                LOG_INFO("Selected prop: {} (ID: 0x{:08X})", prop.name, prop.propID);
            }

            // Tooltip with prop name
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s\nID: 0x%08X", prop.name.c_str(), prop.propID);
            }

            ImGui::EndGroup();

            col++;
            if (col >= columns) {
                col = 0;
            }
        }
    }
    ImGui::EndChild();
}

void PropPainterUI::RenderPaintingControls() {
    ImGui::Text("Painting Controls");
    ImGui::Separator();

    // Rotation selector
    const char* rotationNames[] = { "South (0)", "East (1)", "North (2)", "West (3)" };
    ImGui::Combo("Rotation", &selectedRotation, rotationNames, 4);

    ImGui::Spacing();

    // Thumbnail size slider
    ImGui::SliderInt("Thumbnail Size", &thumbnailSize, 32, 128);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Painting toggle button
    if (!paintingActive) {
        if (ImGui::Button("Start Painting", ImVec2(-1, 40))) {
            if (selectedPropID != 0) {
                paintingActive = true;
                if (callbacks.OnStartPainting) {
                    callbacks.OnStartPainting(selectedPropID, selectedRotation);
                }
                LOG_INFO("Started painting mode for prop 0x{:08X}", selectedPropID);
            } else {
                LOG_WARN("No prop selected");
            }
        }
        if (selectedPropID == 0) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Select a prop first");
        }
    } else {
        if (ImGui::Button("Stop Painting", ImVec2(-1, 40))) {
            paintingActive = false;
            if (callbacks.OnStopPainting) {
                callbacks.OnStopPainting();
            }
            LOG_INFO("Stopped painting mode");
        }
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Painting Active!");
        ImGui::TextWrapped("Click in the city to place props");
    }
}

void PropPainterUI::RenderPropDetails() {
    ImGui::Text("Prop Details");
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

    ImGui::Text("Name: %s", entry->name.c_str());
    ImGui::Text("Prop ID: 0x%08X", entry->propID);
    ImGui::Text("Exemplar IID: 0x%08X", entry->exemplarIID);

    if (entry->s3dType != 0) {
        ImGui::Separator();
        ImGui::Text("S3D Resource:");
        ImGui::Text("  Type: 0x%08X", entry->s3dType);
        ImGui::Text("  Group: 0x%08X", entry->s3dGroup);
        ImGui::Text("  Instance: 0x%08X", entry->s3dInstance);
    }

    // Show larger preview
    if (entry->iconSRV) {
        ImGui::Separator();
        ImGui::Text("Preview:");
        ImGui::Image(
            reinterpret_cast<ImTextureID>(entry->iconSRV),
            ImVec2(128, 128)
        );
    }
}

void PropPainterUI::UpdateLoadingProgress(const char* stage, int current, int total) {
    loadingStage = stage;
    loadingCurrent = current;
    loadingTotal = total;
}
