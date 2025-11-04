#pragma once
#include "PropCacheManager.h"
#include <cstdint>
#include <string>

class PropPainterInputControl;
class cISC43DRender;

/**
 * @brief Callbacks for prop painter UI events
 */
struct PropPainterUICallbacks {
    void (*OnStartPainting)(uint32_t propID, int rotation) = nullptr;
    void (*OnStopPainting)() = nullptr;
    void (*OnBuildCache)() = nullptr;
};

/**
 * @brief ImGui window for the prop painter tool
 */
class PropPainterUI {
public:
    PropPainterUI();
    ~PropPainterUI();

    /**
     * @brief Render the prop painter window
     */
    void Render();

    /**
     * @brief Get pointer to visibility flag for ImGui
     */
    bool* GetShowWindowPtr() { return &showWindow; }

    /**
     * @brief Set the prop cache manager
     */
    void SetPropCacheManager(PropCacheManager* manager) { pCacheManager = manager; }

    /**
     * @brief Set UI event callbacks
     */
    void SetCallbacks(const PropPainterUICallbacks& cb) { callbacks = cb; }

    /**
     * @brief Show/hide the loading window
     */
    void ShowLoadingWindow(bool show) { showLoadingWindow = show; }

    /**
     * @brief Update loading progress
     */
    void UpdateLoadingProgress(const char* stage, int current, int total);

    /**
     * @brief Get currently selected prop ID
     */
    uint32_t GetSelectedPropID() const { return selectedPropID; }

    /**
     * @brief Get currently selected rotation
     */
    int GetSelectedRotation() const { return selectedRotation; }

    /**
     * @brief Check if painting mode is active
     */
    bool IsPaintingActive() const { return paintingActive; }

    /**
     * @brief Set the input control for preview rendering
     */
    void SetInputControl(PropPainterInputControl* pControl) { pInputControl = pControl; }

    /**
     * @brief Set the 3D renderer for coordinate conversion
     */
    void SetRenderer(cISC43DRender* pRender) { pRenderer = pRender; }

    /**
     * @brief Render preview overlay (crosshair, area boundaries, etc.)
     * Call this after the main window render
     */
    void RenderPreviewOverlay();

private:
    void RenderLoadingWindow();
    void RenderPropBrowser();
    void RenderPaintingControls();
    void RenderPropDetails();

    bool showWindow;
    bool showLoadingWindow;
    bool paintingActive;

    PropCacheManager* pCacheManager;
    PropPainterInputControl* pInputControl;
    cISC43DRender* pRenderer;
    PropPainterUICallbacks callbacks;

    // Loading state
    std::string loadingStage;
    int loadingCurrent;
    int loadingTotal;

    // Selection state
    uint32_t selectedPropID;
    int selectedRotation;  // 0-3 (S, E, N, W)

    // UI state
    char searchBuffer[256];
    int thumbnailSize;
    float gridSpacing;
};
