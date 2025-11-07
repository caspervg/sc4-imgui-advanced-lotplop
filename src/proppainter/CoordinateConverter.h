#pragma once
#include "cS3DVector3.h"
#include <cstdint>

class cISC43DRender;

/**
 * @brief Utility for converting between world and screen coordinates
 */
class CoordinateConverter {
public:
    /**
     * @brief Convert world 3D coordinates to screen 2D coordinates
     * @param pRender The 3D renderer (provides matrices and viewport)
     * @param worldPos World position to convert
     * @param screenX Output screen X coordinate
     * @param screenY Output screen Y coordinate
     * @return true if conversion successful (position is visible on screen)
     */
    static bool WorldToScreen(
        cISC43DRender* pRender,
        const cS3DVector3& worldPos,
        float& screenX,
        float& screenY);

private:
    // Matrix math helpers
    static void MultiplyMatrix4x4Vector4(const float* matrix, const float* vec, float* result);
};
