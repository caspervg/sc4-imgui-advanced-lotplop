#pragma once
#include <cstdint>
#include <string>

// Forward declare D3D11 SRV to avoid including d3d11.h here.
struct ID3D11ShaderResourceView;

struct LotConfigEntry {
    uint32_t id;
    std::string name;
    uint32_t sizeX, sizeZ;
    uint16_t minCapacity, maxCapacity;
    uint8_t growthStage;

    // Optional: Item Icon (from building exemplar Item Icon property)
    // Only used for ploppable buildings. SRV owned by the director; UI only reads it.
    ID3D11ShaderResourceView* iconSRV = nullptr;
    int iconWidth = 0;
    int iconHeight = 0;
};
