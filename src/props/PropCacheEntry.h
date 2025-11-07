#pragma once
#include <cstdint>
#include <string>
#include <d3d11.h>

/**
 * @brief Represents a cached prop entry with metadata and thumbnail
 */
struct PropCacheEntry {
    enum class IconType {
        None,
        PNG,
        S3D
    };

    uint32_t propID = 0;              // Prop type ID
    std::string name;                  // Prop name
    uint32_t exemplarIID = 0;          // Exemplar instance ID

    // S3D model resource key (from RKT property)
    uint32_t s3dType = 0;
    uint32_t s3dGroup = 0;
    uint32_t s3dInstance = 0;

    // Thumbnail data
    IconType iconType = IconType::None;
    ID3D11ShaderResourceView* iconSRV = nullptr;
    int iconWidth = 0;
    int iconHeight = 0;

    // Metadata
    uint32_t familyType = 0;           // Prop family (if applicable)

    ~PropCacheEntry() {
        if (iconSRV) {
            iconSRV->Release();
            iconSRV = nullptr;
        }
    }

    // Disable copy to prevent double-free
    PropCacheEntry(const PropCacheEntry&) = delete;
    PropCacheEntry& operator=(const PropCacheEntry&) = delete;

    // Enable move
    PropCacheEntry(PropCacheEntry&& other) noexcept
        : propID(other.propID)
        , name(std::move(other.name))
        , exemplarIID(other.exemplarIID)
        , s3dType(other.s3dType)
        , s3dGroup(other.s3dGroup)
        , s3dInstance(other.s3dInstance)
        , iconType(other.iconType)
        , iconSRV(other.iconSRV)
        , iconWidth(other.iconWidth)
        , iconHeight(other.iconHeight)
        , familyType(other.familyType)
    {
        other.iconSRV = nullptr;
    }

    PropCacheEntry& operator=(PropCacheEntry&& other) noexcept {
        if (this != &other) {
            if (iconSRV) {
                iconSRV->Release();
            }
            propID = other.propID;
            name = std::move(other.name);
            exemplarIID = other.exemplarIID;
            s3dType = other.s3dType;
            s3dGroup = other.s3dGroup;
            s3dInstance = other.s3dInstance;
            iconType = other.iconType;
            iconSRV = other.iconSRV;
            iconWidth = other.iconWidth;
            iconHeight = other.iconHeight;
            familyType = other.familyType;
            other.iconSRV = nullptr;
        }
        return *this;
    }

    PropCacheEntry() = default;
};
