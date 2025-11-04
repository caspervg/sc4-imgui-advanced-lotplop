#pragma once
#include "cSC4BaseViewInputControl.h"
#include "cRZAutoRefCount.h"
#include "cS3DVector3.h"
#include <cstdint>
#include <string>

class cISC4City;
class cISC4PropManager;

/**
 * @brief Preview state for rendering overlays
 */
struct PropPainterPreviewState {
    bool cursorValid = false;
    cS3DVector3 cursorWorldPos;
    std::string propName;
    uint32_t propID = 0;
    int32_t rotation = 0;

    // Area fill mode
    bool isDefiningArea = false;
    cS3DVector3 areaStart;
    cS3DVector3 areaEnd;
};

/**
 * @brief View input control for painting props in the 3D view
 *
 * This control handles mouse clicks to place props at the clicked location
 * in the city.
 */
class PropPainterInputControl : public cSC4BaseViewInputControl {
public:
    PropPainterInputControl();
    virtual ~PropPainterInputControl();

    // cISC4ViewInputControl overrides
    bool Init() override;
    bool Shutdown() override;

    bool OnMouseDownL(int32_t x, int32_t z, uint32_t modifiers) override;
    bool OnMouseMove(int32_t x, int32_t z, uint32_t modifiers) override;
    bool OnKeyDown(int32_t vkCode, uint32_t modifiers) override;

    void Activate() override;
    void Deactivate() override;

    /**
     * @brief Set the prop to paint
     * @param propID The prop type ID
     * @param rotation The orientation (0-3)
     */
    void SetPropToPaint(uint32_t propID, int32_t rotation, const std::string& name);

    /**
     * @brief Set the city instance
     */
    void SetCity(cISC4City* pCity);

    /**
     * @brief Get current preview state for UI rendering
     */
    const PropPainterPreviewState& GetPreviewState() const { return previewState; }

private:
    /**
     * @brief Place a prop at the given screen coordinates
     */
    bool PlacePropAt(int32_t screenX, int32_t screenZ);

    /**
     * @brief Convert screen coordinates to world coordinates
     * @return true if conversion was successful
     */
    bool ScreenToWorld(int32_t screenX, int32_t screenZ, float& worldX, float& worldZ);

    /**
     * @brief Update preview state based on current mouse position
     */
    void UpdatePreviewState(int32_t screenX, int32_t screenZ);

    cRZAutoRefCount<cISC4City> city;
    cRZAutoRefCount<cISC4PropManager> propManager;

    uint32_t propIDToPaint;
    int32_t rotationToPaint;
    bool isPainting;

    PropPainterPreviewState previewState;
};
