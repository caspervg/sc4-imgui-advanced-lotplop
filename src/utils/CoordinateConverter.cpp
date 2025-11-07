#include "CoordinateConverter.h"

#include <cmath>

#include "cISC43DRender.h"
#include "cS3DVector3.h"
#include "Logger.h"

bool CoordinateConverter::WorldToScreen(
    cISC43DRender* pRender,
    const cS3DVector3& worldPos,
    float& screenX,
    float& screenY)
{
    if (!pRender) {
        return false;
    }

    // Get projection and view matrices
    const auto* projMatrix = reinterpret_cast<float*>(pRender->GetProjectionMatrixEntries());
    const auto* viewMatrix = reinterpret_cast<float*>(pRender->GetViewMatrixEntities());

    if (!projMatrix || !viewMatrix) {
        LOG_DEBUG("Failed to get projection/view matrices");
        return false;
    }

    // Get viewport dimensions
    uint32_t viewportW = 0, viewportH = 0;
    if (!pRender->GetViewportSize(viewportW, viewportH)) {
        return false;
    }

    // Transform pipeline: World -> View -> Clip -> NDC -> Screen

    // 1. Transform world position to view space (camera space)
    const float viewVec[4] = { worldPos.fX, worldPos.fY, worldPos.fZ, 1.0f };
    float viewResult[4];
    MultiplyMatrix4x4Vector4(viewMatrix, viewVec, viewResult);

    // 2. Transform view space to clip space
    float clipResult[4];
    MultiplyMatrix4x4Vector4(projMatrix, viewResult, clipResult);

    // 3. Check if point is behind camera
    if (clipResult[3] <= 0.0f) {
        return false;  // Behind camera or at camera position
    }

    // 4. Perspective divide to get NDC (-1 to 1)
    float ndcX = clipResult[0] / clipResult[3];
    float ndcY = clipResult[1] / clipResult[3];
    float ndcZ = clipResult[2] / clipResult[3];

    // Check if point is outside view frustum
    if (ndcX < -1.0f || ndcX > 1.0f ||
        ndcY < -1.0f || ndcY > 1.0f ||
        ndcZ < -1.0f || ndcZ > 1.0f) {
        return false;  // Outside view frustum
    }

    // 5. Convert NDC to screen coordinates
    // NDC: -1 to 1 -> Screen: 0 to viewport size
    screenX = (ndcX * 0.5f + 0.5f) * static_cast<float>(viewportW);
    screenY = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(viewportH);  // Flip Y axis

    return true;
}

void CoordinateConverter::MultiplyMatrix4x4Vector4(const float* matrix, const float* vec, float* result) {
    // Matrix is in row-major or column-major format (need to verify in SC4)
    // Assuming column-major (OpenGL style): result = matrix * vec

    result[0] = matrix[0] * vec[0] + matrix[4] * vec[1] + matrix[8]  * vec[2] + matrix[12] * vec[3];
    result[1] = matrix[1] * vec[0] + matrix[5] * vec[1] + matrix[9]  * vec[2] + matrix[13] * vec[3];
    result[2] = matrix[2] * vec[0] + matrix[6] * vec[1] + matrix[10] * vec[2] + matrix[14] * vec[3];
    result[3] = matrix[3] * vec[0] + matrix[7] * vec[1] + matrix[11] * vec[2] + matrix[15] * vec[3];
}
