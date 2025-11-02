#pragma once
#include <cstdint>
#include <string>
#include <unordered_set>

// Forward declare D3D11 SRV to avoid including d3d11.h here.
struct ID3D11ShaderResourceView;

struct LotConfigEntry {
    // Icon type enumeration
    enum class IconType : uint8_t {
        None = 0,       // No icon available
        PNG = 1,        // PNG menu icon (176x44, show middle 44x44)
        S3D = 2         // S3D thumbnail (square, typically 64x64)
    };

    uint32_t id;
    std::string name;
    std::string description; // Item Description (localized if available)
    uint32_t sizeX, sizeZ;
    uint16_t minCapacity, maxCapacity;
    uint8_t growthStage;

    // Building classification
    std::unordered_set<uint32_t> occupantGroups; // Raw occupant group IDs

    // Building reference (S3D model information)
    uint32_t buildingExemplarID = 0;  // Building exemplar instance ID
    uint32_t s3dInstance = 0;          // Calculated S3D resource instance (from RKT properties)
    uint32_t s3dType = 0;              // S3D type ID (usually 0x5AD0E817)
    uint32_t s3dGroup = 0;             // S3D group ID (building family)

    // Item Icon instance (PNG resource instance id) saved during cache build
    uint32_t iconInstance = 0;

    // Unified icon/thumbnail SRV (either PNG icon or S3D thumbnail, never both)
    // SRV owned by the cache manager; UI only reads it.
    ID3D11ShaderResourceView* iconSRV = nullptr;
    IconType iconType = IconType::None;

    // Dimensions - interpretation depends on iconType:
    // - PNG: iconWidth=176, iconHeight=44 (full sprite sheet)
    // - S3D: iconWidth=iconHeight=size (square thumbnail, e.g., 64x64)
    int iconWidth = 0;
    int iconHeight = 0;

    // Lazy load state (set by director when a decode job is queued)
    bool iconRequested = false;
    bool descriptionLoaded = false;
};
