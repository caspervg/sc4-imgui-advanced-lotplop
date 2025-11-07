#include "PropCacheManager.h"

#include <d3d11.h>

#include "cGZPersistResourceKey.h"
#include "cIGZPersistResourceManager.h"
#include "cIGZString.h"
#include "cISC4City.h"
#include "cISC4PropManager.h"
#include "cISCPropertyHolder.h"
#include "cRZAutoRefCount.h"
#include "SC4Vector.h"
#include "../exemplar/PropertyUtil.h"
#include "../s3d/S3DThumbnailGenerator.h"
#include "../utils/Logger.h"

static constexpr uint32_t kResourceKeyType1 = 0x27812821; // RKT1

PropCacheManager::PropCacheManager()
    : initialized(false)
    , pPropManager(nullptr)
    , progressCallback(nullptr)
{
}

PropCacheManager::~PropCacheManager() {
    Clear();
}

void PropCacheManager::Clear() {
    props.clear();
    propIDToIndex.clear();
    familyTypes.clear();
    pPropManager = nullptr;
    initialized = false;
}

bool PropCacheManager::Initialize(
    cISC4City* pCity,
    cIGZPersistResourceManager* pRM,
    ID3D11Device* pDevice,
    ID3D11DeviceContext* pContext,
    ProgressCallback callback)
{
    if (initialized) {
        LOG_WARN("PropCacheManager already initialized");
        return true;
    }

    if (!pCity || !pRM) {
        LOG_ERROR("PropCacheManager::Initialize - null parameters");
        return false;
    }

    progressCallback = callback;

    LOG_INFO("Initializing prop cache...");

    this->pPropManager = pCity->GetPropManager();
    if (!this->pPropManager) {
        LOG_ERROR("Failed to get PropManager from city");
        return false;
    }

    // Load prop family types
    SC4Vector<uint32_t> families;
    this->pPropManager->GetAllPropFamilyTypes(families);
    familyTypes.assign(families.begin(), families.end());
    LOG_INFO("Found {} prop families", familyTypes.size());

    bool result = LoadPropsFromManager(this->pPropManager, pRM, pDevice, pContext);

    if (result) {
        LOG_INFO("Prop cache initialized with {} props", props.size());
        initialized = true;
    } else {
        LOG_ERROR("Failed to initialize prop cache");
        Clear();
    }

    progressCallback = nullptr;
    return result;
}

bool PropCacheManager::LoadPropsFromManager(
    cISC4PropManager* pPropManager,
    cIGZPersistResourceManager* pRM,
    ID3D11Device* pDevice,
    ID3D11DeviceContext* pContext)
{
    // Get all prop types from the manager
    SC4Vector<uint32_t> propTypes;
    pPropManager->GetAllPropTypes(propTypes);

    if (propTypes.size() == 0) {
        LOG_WARN("No props found in PropManager");
        return true; // Not an error, just no props
    }

    LOG_INFO("Found {} prop types", propTypes.size());

    int currentIdx = 0;
    int total = static_cast<int>(propTypes.size());

    for (uint32_t propID : propTypes) {
        currentIdx++;

        // Report progress
        if (progressCallback && currentIdx % 10 == 0) {
            progressCallback("Loading props", currentIdx, total);
        }

        PropCacheEntry entry;
        entry.propID = propID;

        // Get prop name
        // cIGZString* pName = pPropManager->GetPropName(propID);
        // if (pName) {
        //     entry.name = pName->ToChar();
        // } else {
        //     entry.name = "Unknown Prop";
        // }

        // Get the exemplar resource key for this prop
        cGZPersistResourceKey exemplarKey;
        if (!pPropManager->GetPropKeyFromType(propID, exemplarKey)) {
            LOG_DEBUG("Failed to get resource key for prop 0x{:08X}", propID);
            continue;
        }

        entry.exemplarIID = exemplarKey.instance;

        // Load the prop exemplar
        cRZAutoRefCount<cISCPropertyHolder> pPropExemplar;
        if (!pRM->GetResource(
            exemplarKey,
            GZIID_cISCPropertyHolder,
            pPropExemplar.AsPPVoid(),
            0,
            nullptr))
        {
            LOG_DEBUG("Failed to load exemplar for prop 0x{:08X}", propID);
            continue;
        }

    	constexpr uint32_t kPropExemplarName = 0x00000020;
    	const auto propName = new cRZBaseString(64);
    	pPropExemplar->GetProperty(kPropExemplarName, *propName);
    	entry.name = propName->ToChar();

        // Extract S3D resource key from RKT1 property
        cGZPersistResourceKey s3dKey;
        if (PropertyUtil::GetPropertyResourceKey(
            pPropExemplar,
            kResourceKeyType1,
            s3dKey))
        {
            entry.s3dType = s3dKey.type;
            entry.s3dGroup = s3dKey.group;
            entry.s3dInstance = s3dKey.instance;

            // Generate S3D thumbnail if D3D11 is available
            if (pDevice && pContext) {
                ID3D11ShaderResourceView* s3dSRV =
                    S3D::ThumbnailGenerator::GenerateThumbnailFromExemplar(
                        pPropExemplar,
                        pRM,
                        pDevice,
                        pContext,
                        64,  // thumbnail size
                        5,   // zoom level (closest)
                        0    // rotation (south)
                    );

                if (s3dSRV) {
                    entry.iconSRV = s3dSRV;
                    entry.iconWidth = 64;
                    entry.iconHeight = 64;
                    entry.iconType = PropCacheEntry::IconType::S3D;
                }
            }
        }

        // Store the entry
        propIDToIndex[propID] = props.size();
        props.push_back(std::move(entry));
    }

    LOG_INFO("Successfully loaded {} props with thumbnails", props.size());
    return true;
}

const PropCacheEntry* PropCacheManager::GetPropByID(uint32_t propID) const {
    auto it = propIDToIndex.find(propID);
    if (it != propIDToIndex.end()) {
        return &props[it->second];
    }
    return nullptr;
}
