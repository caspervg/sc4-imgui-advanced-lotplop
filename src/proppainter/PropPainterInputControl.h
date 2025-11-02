#pragma once
#include "cSC4BaseViewInputControl.h"
#include "cRZAutoRefCount.h"
#include <cstdint>

class cISC4City;
class cISC4PropManager;

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
    void SetPropToPaint(uint32_t propID, int32_t rotation);

    /**
     * @brief Set the city instance
     */
    void SetCity(cISC4City* pCity);

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

    cRZAutoRefCount<cISC4City> city;
    cRZAutoRefCount<cISC4PropManager> propManager;

    uint32_t propIDToPaint;
    int32_t rotationToPaint;
    bool isPainting;
};
