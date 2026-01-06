#pragma once
#include <cstdint>

class cISCPropertyHolder;
class cIGZPersistResourceManager;
class cIGZImGuiService;
struct IDirectDrawSurface7;

namespace S3D {

class ThumbnailGeneratorDX7 {
public:
    static IDirectDrawSurface7* GenerateThumbnailFromExemplar(
        cISCPropertyHolder* pBuildingExemplar,
        cIGZPersistResourceManager* pRM,
        cIGZImGuiService* pImGuiService,
        int thumbnailSize = 64,
        int zoomLevel = 5,
        int rotation = 0
    );
};

} // namespace S3D
