#pragma once
#include <cstdint>
#include <string>
#include <unordered_set>

// Forward declare D3D11 SRV to avoid including d3d11.h here.
struct IDirectDrawSurface7; // DX7 texture surface

struct LotConfigEntry {
    uint32_t id;
    std::string name;
    std::string description; // Item Description (localized if available)
    uint32_t sizeX, sizeZ;
    uint16_t minCapacity, maxCapacity;
    uint8_t growthStage;

    // Building classification
    std::unordered_set<uint32_t> occupantGroups; // Raw occupant group IDs

    // Item Icon instance (PNG resource instance id) saved during cache build
    uint32_t iconInstance = 0;

    // Optional: Item Icon surface (decoded lazily)
    // Only used for ploppable buildings. Surface owned by the director; UI only reads it.
    IDirectDrawSurface7* iconSRV = nullptr; // DirectDraw surface used as ImTextureID
    int iconWidth = 0;
    int iconHeight = 0;

    // Lazy load state (set by director when a decode job is queued)
    bool iconRequested = false;
    bool descriptionLoaded = false;
};
