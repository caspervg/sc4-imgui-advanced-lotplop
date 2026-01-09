#pragma once
#include <cstdint>
#include <string>
#include "public/cIGZImGuiService.h"

/**
 * @brief Represents a cached prop entry with metadata and thumbnail
 */
struct PropCacheEntry {
    enum class IconType {
        None,
        PNG,
        S3D
    };

    uint32_t propID = 0;              // Prop type ID (same as exemplarIID)
    std::string name;                 // Prop name
    uint32_t exemplarIID = 0;         // Exemplar instance ID (same as propID)

    // Exemplar resource key (for cache persistence)
    uint32_t exemplarGroup = 0;       // Exemplar group ID (exemplarIID IS the instance)

    // S3D model resource key (from RKT property)
    uint32_t s3dType = 0;
    uint32_t s3dGroup = 0;
    uint32_t s3dInstance = 0;

    // Thumbnail data
    IconType iconType = IconType::None;
    ImGuiTextureHandle iconHandle{0, 0};
    int iconWidth = 0;
    int iconHeight = 0;

    // Metadata
    uint32_t familyType = 0;          // Prop family (if applicable)

    PropCacheEntry() = default;
};

