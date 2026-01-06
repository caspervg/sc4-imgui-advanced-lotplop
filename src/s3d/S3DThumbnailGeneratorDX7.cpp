#include "S3DThumbnailGeneratorDX7.h"
#include "S3DThumbnailGenerator.h"
#include "S3DReader.h"
#include "S3DRendererDX7.h"
#include "cGZPersistResourceKey.h"
#include "cIGZPersistDBRecord.h"
#include "cIGZPersistResourceManager.h"
#include "cIGZVariant.h"
#include "cISCProperty.h"
#include "cISCPropertyHolder.h"
#include "public/cIGZImGuiService.h"
#include "../utils/Logger.h"
#include <vector>

namespace S3D {

IDirectDrawSurface7* ThumbnailGeneratorDX7::GenerateThumbnailFromExemplar(
    cISCPropertyHolder* pBuildingExemplar,
    cIGZPersistResourceManager* pRM,
    cIGZImGuiService* pImGuiService,
    int thumbnailSize,
    int zoomLevel,
    int rotation
) {
    if (!pBuildingExemplar || !pRM || !pImGuiService) {
        LOG_DEBUG("S3D DX7 thumbnail: Invalid parameters");
        return nullptr;
    }

    uint32_t s3dType = 0;
    uint32_t s3dGroup = 0;
    uint32_t baseInstance = 0;

    if (!S3D::ThumbnailGenerator::GetS3DResourceKey(pBuildingExemplar, s3dType, s3dGroup, baseInstance)) {
        LOG_DEBUG("S3D DX7 thumbnail: No RKT property found in building exemplar");
        return nullptr;
    }

    // Determine final instance based on RKT type
    uint32_t finalInstance = baseInstance;
    constexpr uint32_t kResourceKeyType0 = 0x27812820;
    constexpr uint32_t kResourceKeyType1 = 0x27812821;
    constexpr uint32_t kResourceKeyType2 = 0x27812822;
    constexpr uint32_t kResourceKeyType3 = 0x27812823;
    constexpr uint32_t kResourceKeyType5 = 0x27812825;

    const cISCProperty* rktProp = nullptr;
    if ((rktProp = pBuildingExemplar->GetProperty(kResourceKeyType1)) != nullptr) {
        finalInstance = S3D::ThumbnailGenerator::CalculateS3DInstance(baseInstance, zoomLevel, rotation);
    } else if ((rktProp = pBuildingExemplar->GetProperty(kResourceKeyType0)) != nullptr) {
        finalInstance = baseInstance;
    } else if ((rktProp = pBuildingExemplar->GetProperty(kResourceKeyType2)) != nullptr) {
        const cIGZVariant* val = rktProp->GetPropertyValue();
        if (val && val->GetType() == cIGZVariant::Type::Uint32Array) {
            const uint32_t* vals = val->RefUint32();
            int clampedZoom = (zoomLevel < 1) ? 1 : (zoomLevel > 5) ? 5 : zoomLevel;
            int clampedRot = (rotation < 0) ? 0 : (rotation > 3) ? 3 : rotation;
            int index = 2 + (clampedZoom - 1) * 4 + clampedRot;
            if (index < static_cast<int>(val->GetCount())) {
                finalInstance = vals[index];
            }
        }
    } else if ((rktProp = pBuildingExemplar->GetProperty(kResourceKeyType3)) != nullptr) {
        const cIGZVariant* val = rktProp->GetPropertyValue();
        if (val && val->GetType() == cIGZVariant::Type::Uint32Array) {
            const uint32_t* vals = val->RefUint32();
            int clampedZoom = (zoomLevel < 1) ? 1 : (zoomLevel > 5) ? 5 : zoomLevel;
            int index = 2 + (clampedZoom - 1);
            if (index < static_cast<int>(val->GetCount())) {
                finalInstance = vals[index];
            }
        }
    } else if ((rktProp = pBuildingExemplar->GetProperty(kResourceKeyType5)) != nullptr) {
        finalInstance = S3D::ThumbnailGenerator::CalculateS3DInstance(baseInstance, zoomLevel, rotation);
    }

    cGZPersistResourceKey s3dKey(s3dType, s3dGroup, finalInstance);
    cIGZPersistDBRecord* pRecord = nullptr;
    if (!pRM->OpenDBRecord(s3dKey, &pRecord, false)) {
        LOG_DEBUG("S3D DX7 thumbnail: S3D resource not found - TGI {:08X}-{:08X}-{:08X}",
                  s3dType, s3dGroup, finalInstance);
        return nullptr;
    }

    uint32_t dataSize = pRecord->GetSize();
    if (dataSize == 0) {
        pRecord->Close();
        return nullptr;
    }

    std::vector<uint8_t> s3dData(dataSize);
    if (!pRecord->GetFieldVoid(s3dData.data(), dataSize)) {
        pRecord->Close();
        return nullptr;
    }
    pRecord->Close();

    S3D::Model model;
    if (!S3D::Reader::Parse(s3dData.data(), dataSize, model)) {
        return nullptr;
    }

    IDirect3DDevice7* d3d = nullptr;
    IDirectDraw7* dd = nullptr;
    if (!pImGuiService->AcquireD3DInterfaces(&d3d, &dd)) {
        return nullptr;
    }

    S3D::RendererDX7 renderer(d3d, dd);
    if (!renderer.LoadModel(model, pRM, s3dGroup)) {
        d3d->Release();
        dd->Release();
        return nullptr;
    }

    IDirectDrawSurface7* surface = renderer.GenerateThumbnail(thumbnailSize);

    d3d->Release();
    dd->Release();

    return surface;
}

} // namespace S3D
