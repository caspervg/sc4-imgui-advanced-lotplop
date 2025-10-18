#pragma once
#include <cstdint>
#include <string>

struct LotConfigEntry {
    uint32_t id;
    std::string name;
    uint32_t sizeX, sizeZ;
    uint16_t minCapacity, maxCapacity;
    uint8_t growthStage;
};
