#include "PropPainterInputControl.h"
#include "cISC4City.h"
#include "cISC4PropManager.h"
#include "cISC4View3DWin.h"
#include "cS3DVector3.h"
#include "../utils/Logger.h"
#include <windows.h>

// Unique ID for this control (randomly generated)
static const uint32_t kPropPainterControlID = 0x8A3F9D2B;

PropPainterInputControl::PropPainterInputControl()
    : cSC4BaseViewInputControl(kPropPainterControlID)
    , propIDToPaint(0)
    , rotationToPaint(0)
    , isPainting(false)
{
}

PropPainterInputControl::~PropPainterInputControl() {
}

bool PropPainterInputControl::Init() {
    if (!cSC4BaseViewInputControl::Init()) {
        return false;
    }

    LOG_INFO("PropPainterInputControl initialized");
    return true;
}

bool PropPainterInputControl::Shutdown() {
    LOG_INFO("PropPainterInputControl shutting down");
    return cSC4BaseViewInputControl::Shutdown();
}

void PropPainterInputControl::Activate() {
    cSC4BaseViewInputControl::Activate();
    isPainting = true;
    LOG_INFO("PropPainterInputControl activated");
}

void PropPainterInputControl::Deactivate() {
    isPainting = false;
    cSC4BaseViewInputControl::Deactivate();
    LOG_INFO("PropPainterInputControl deactivated");
}

void PropPainterInputControl::SetPropToPaint(uint32_t propID, int32_t rotation, const std::string& name) {
    propIDToPaint = propID;
    rotationToPaint = rotation;
    previewState.propID = propID;
    previewState.rotation = rotation;
    previewState.propName = name;
    LOG_INFO("Set prop to paint: {} (0x{:08X}), rotation: {}", name, propID, rotation);
}

void PropPainterInputControl::SetCity(cISC4City* pCity) {
    city = pCity;
    if (pCity) {
        propManager = pCity->GetPropManager();
    }
}

bool PropPainterInputControl::OnMouseDownL(int32_t x, int32_t z, uint32_t modifiers) {
    if (!isPainting || propIDToPaint == 0) {
        return false;
    }

    return PlacePropAt(x, z);
}

bool PropPainterInputControl::OnMouseMove(int32_t x, int32_t z, uint32_t modifiers) {
    if (!isPainting) {
        return false;
    }

    UpdatePreviewState(x, z);
    return true;
}

void PropPainterInputControl::UpdatePreviewState(int32_t screenX, int32_t screenZ) {
    if (!view3D) {
        previewState.cursorValid = false;
        return;
    }

    // Update cursor world position
    float worldCoords[3] = { 0.0f, 0.0f, 0.0f };
    previewState.cursorValid = view3D->PickTerrain(screenX, screenZ, worldCoords, false);

    if (previewState.cursorValid) {
        previewState.cursorWorldPos.fX = worldCoords[0];
        previewState.cursorWorldPos.fY = worldCoords[1];
        previewState.cursorWorldPos.fZ = worldCoords[2];

        // Update rotation from current state
        previewState.rotation = rotationToPaint;
    }
}

bool PropPainterInputControl::OnKeyDown(int32_t vkCode, uint32_t modifiers) {
    // ESC to cancel painting
    if (vkCode == VK_ESCAPE) {
        LOG_INFO("PropPainterInputControl: ESC pressed, ending input");
        EndInput();
        return true;
    }

    // R to rotate
    if (vkCode == 'R') {
        rotationToPaint = (rotationToPaint + 1) % 4;
        LOG_INFO("Rotated to: {}", rotationToPaint);
        return true;
    }

    return false;
}

bool PropPainterInputControl::PlacePropAt(int32_t screenX, int32_t screenZ) {
    if (!propManager || !view3D) {
        LOG_ERROR("PropPainterInputControl: PropManager or View3D not available");
        return false;
    }

    // Convert screen coordinates to world coordinates
    float worldCoords[3] = { 0.0f, 0.0f, 0.0f };
    if (!view3D->PickTerrain(screenX, screenZ, worldCoords, false)) {
        LOG_DEBUG("Failed to pick terrain at screen ({}, {})", screenX, screenZ);
        return false;
    }

    // Create position vector
    cS3DVector3 position(worldCoords[0], worldCoords[1], worldCoords[2]);

    LOG_INFO("Placing prop 0x{:08X} at ({:.2f}, {:.2f}, {:.2f}), rotation: {}",
        propIDToPaint, position.fX, position.fY, position.fZ, rotationToPaint);

    // Add the prop to the city
    bool success = propManager->AddCityProp(propIDToPaint, position, rotationToPaint);

    if (success) {
        LOG_INFO("Successfully placed prop");
    } else {
        LOG_ERROR("Failed to place prop");
    }

    return success;
}

bool PropPainterInputControl::ScreenToWorld(int32_t screenX, int32_t screenZ, float& worldX, float& worldZ) {
    if (!view3D) {
        return false;
    }

    float worldCoords[3] = { 0.0f, 0.0f, 0.0f };
    if (!view3D->PickTerrain(screenX, screenZ, worldCoords, false)) {
        return false;
    }

    worldX = worldCoords[0];
    worldZ = worldCoords[2];
    return true;
}
