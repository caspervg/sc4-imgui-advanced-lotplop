#include "IconResourceUtil.h"
#include "cIGZPersistResourceManager.h"
#include "cGZPersistResourceKey.h"
#include "cIGZPersistDBRecord.h"
#include "SCPropertyUtil.h"

namespace ExemplarUtil {

bool GetItemIconInstance(cISCPropertyHolder* pBuildingExemplar, uint32_t& out_instance)
{
    if (!pBuildingExemplar) return false;

    constexpr uint32_t kItemIconProperty = 0x8A2602B8; // Item Icon
    uint32_t instance = 0;
    if (SCPropertyUtil::GetPropertyValue(pBuildingExemplar, kItemIconProperty, instance))
    {
        out_instance = instance;
        return (instance != 0);
    }
    return false;
}

bool LoadPNGByInstance(cIGZPersistResourceManager* pRM, uint32_t instance, std::vector<uint8_t>& bytes)
{
    if (!pRM || instance == 0) return false;

    // SC4 PNG resource type
    constexpr uint32_t kPNGType = 0x856DDBAC;
    // Lot icon group per spec
    uint32_t group = 0x6A386D26;

    cGZPersistResourceKey key(kPNGType, group, instance);

    cIGZPersistDBRecord* pRecord = nullptr;
    // false = unknown flag (do not create?), open existing
    if (!pRM->OpenDBRecord(key, &pRecord, false) || !pRecord)
        return false;

    uint32_t size = pRecord->GetSize();
    if (size == 0) {
        pRM->CloseDBRecord(key, &pRecord);
        return false;
    }

    bytes.resize(size);
    bool ok = pRecord->GetFieldVoid(bytes.data(), size);

    pRM->CloseDBRecord(key, &pRecord);

    return ok;
}

} // namespace ExemplarUtil
