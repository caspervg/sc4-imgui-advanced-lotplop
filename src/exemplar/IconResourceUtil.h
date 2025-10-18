
#pragma once
#include <cstdint>
#include <vector>

class cIGZPersistResourceManager;
class cISCPropertyHolder;

namespace ExemplarUtil {
    // Attempts to read the Item Icon (property 0x8A2602B8) from the given building exemplar.
    // On success returns true and fills out_instance with the PNG resource instance id.
    bool GetItemIconInstance(cISCPropertyHolder* pBuildingExemplar, uint32_t& out_instance);

    // Loads the PNG resource bytes for given PNG type (0x856DDBAC) and instance.
    // Returns true and fills bytes on success.
    bool LoadPNGByInstance(cIGZPersistResourceManager* pRM, uint32_t instance, std::vector<uint8_t>& bytes);
}
