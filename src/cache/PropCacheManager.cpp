#define NOMINMAX
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
#include "../gfx/DX11ImageLoader.h"
#include "../gfx/TextureToPNG.h"
#include "../utils/Logger.h"
#include "CacheDatabase.h"

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

bool PropCacheManager::BeginIncrementalBuild(cISC4City* pCity) {
    if (initialized) {
        LOG_WARN("PropCacheManager already initialized");
        return false;
    }

    if (!pCity) {
        LOG_ERROR("PropCacheManager::BeginIncrementalBuild - no city provided");
        return false;
    }

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

    // Get all prop types to process
    SC4Vector<uint32_t> propTypes;
    this->pPropManager->GetAllPropTypes(propTypes);
    propTypesToProcess.assign(propTypes.begin(), propTypes.end());

    if (propTypesToProcess.empty()) {
        LOG_WARN("No props found in PropManager");
        return true; // Not an error, just no props
    }

    LOG_INFO("Found {} prop types for processing", propTypesToProcess.size());

    totalPropCount = static_cast<int>(propTypesToProcess.size());
    currentPropIndex = 0;
    processedPropCount = 0;

    return true;
}

int PropCacheManager::ProcessPropBatch(
    cIGZPersistResourceManager* pRM,
    ID3D11Device* pDevice,
    ID3D11DeviceContext* pContext,
    int batchSize)
{
    if (propTypesToProcess.empty() || currentPropIndex >= static_cast<int>(propTypesToProcess.size())) {
        return 0;
    }

    int processed = 0;
    int endIndex = std::min(currentPropIndex + batchSize, static_cast<int>(propTypesToProcess.size()));

    for (int i = currentPropIndex; i < endIndex; ++i) {
        uint32_t propID = propTypesToProcess[i];
        if (ProcessPropEntry(propID, pRM, pDevice, pContext)) {
            processed++;
            processedPropCount++;
        }
        currentPropIndex++;
    }

    // Report progress
    if (progressCallback && processedPropCount % 10 == 0) {
        progressCallback("Loading props", processedPropCount, totalPropCount);
    }

    return processed;
}

bool PropCacheManager::IsProcessingComplete() const {
    return propTypesToProcess.empty() || currentPropIndex >= static_cast<int>(propTypesToProcess.size());
}

void PropCacheManager::FinalizeIncrementalBuild() {
    LOG_INFO("Finalizing prop cache with {} props", props.size());
    propTypesToProcess.clear();
    currentPropIndex = 0;
    processedPropCount = 0;
    totalPropCount = 0;
    progressCallback = nullptr;
    initialized = true;
}

bool PropCacheManager::ProcessPropEntry(
    uint32_t propID,
    cIGZPersistResourceManager* pRM,
    ID3D11Device* pDevice,
    ID3D11DeviceContext* pContext)
{
    PropCacheEntry entry;
    entry.propID = propID;

    // Get the exemplar resource key for this prop
    cGZPersistResourceKey exemplarKey;
    if (!pPropManager->GetPropKeyFromType(propID, exemplarKey)) {
        LOG_DEBUG("Failed to get resource key for prop 0x{:08X}", propID);
        return false;
    }

    entry.exemplarIID = exemplarKey.instance;
    entry.exemplarGroup = exemplarKey.group;

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
        return false;
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
    return true;
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

        ProcessPropEntry(propID, pRM, pDevice, pContext);
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

bool PropCacheManager::LoadFromDatabase(const std::filesystem::path& dbPath, ID3D11Device* pDevice, ID3D11DeviceContext* pContext) {
    if (!pDevice || !pContext) {
        Logger::LOG_ERROR("Invalid device or context for prop cache loading");
        return false;
    }

    CacheDatabase db;
    if (!db.OpenOrCreate(dbPath)) {
        Logger::LOG_ERROR("Failed to open cache database: {}", dbPath.string());
        return false;
    }

    // Validate cache version
    std::string version = db.GetMetadata("cache_version");
    if (version != "1") {
        Logger::LOG_WARN("Cache version mismatch (expected 1, got {}), rebuild required", version);
        return false;
    }

    // Load all prop keys
    auto keys = db.GetAllPropKeys();
    if (keys.empty()) {
        Logger::LOG_WARN("Cache database has no props");
        return false;
    }

    Logger::LOG_INFO("Loading {} props from cache database...", keys.size());

    int loadedCount = 0;
    for (auto [group, instance] : keys) {
        auto result = db.LoadProp(group, instance);
        if (!result) {
            Logger::LOG_WARN("Failed to load prop {}/{} from database", group, instance);
            continue;
        }

        auto& [prop, pngBlob] = *result;

        // Create texture from PNG BLOB
        if (!pngBlob.empty()) {
            ID3D11ShaderResourceView* srv = nullptr;
            int w = 0, h = 0;
            if (gfx::CreateSRVFromPNGMemory(pngBlob.data(), pngBlob.size(), pDevice, &srv, &w, &h)) {
                prop.iconSRV = srv;
                prop.iconWidth = w;
                prop.iconHeight = h;
            } else {
                Logger::LOG_WARN("Failed to decode PNG thumbnail for prop {}", prop.propID);
            }
        }

        // Store in cache
        size_t index = props.size();
        props.push_back(std::move(prop));
        propIDToIndex[props[index].propID] = index;
        loadedCount++;
    }

    initialized = (loadedCount > 0);
    Logger::LOG_INFO("Loaded {} props from cache database in {}", loadedCount, dbPath.string());
    return initialized;
}

bool PropCacheManager::SaveToDatabase(const std::filesystem::path& dbPath, ID3D11Device* pDevice, ID3D11DeviceContext* pContext) {
    if (!pDevice || !pContext) {
        Logger::LOG_ERROR("Invalid device or context for prop cache saving");
        return false;
    }

    if (!initialized || props.empty()) {
        Logger::LOG_WARN("Prop cache not initialized or empty, nothing to save");
        return false;
    }

    CacheDatabase db;
    if (!db.OpenOrCreate(dbPath)) {
        Logger::LOG_ERROR("Failed to open cache database for saving: {}", dbPath.string());
        return false;
    }

    Logger::LOG_INFO("Saving {} props to cache database...", props.size());

    // Use transaction for bulk insert (much faster)
    if (!db.BeginTransaction()) {
        Logger::LOG_ERROR("Failed to begin transaction");
        return false;
    }

    int savedCount = 0;
    int thumbnailCount = 0;
    for (const auto& prop : props) {
        // Encode thumbnail to PNG
        std::vector<uint8_t> pngBlob;
        if (prop.iconSRV) {
            ID3D11Resource* resource = nullptr;
            prop.iconSRV->GetResource(&resource);
            if (resource) {
                ID3D11Texture2D* texture = nullptr;
                HRESULT hr = resource->QueryInterface(__uuidof(ID3D11Texture2D),
                                                       reinterpret_cast<void**>(&texture));
                if (SUCCEEDED(hr) && texture) {
                    if (TextureToPNG::Encode(pDevice, pContext, texture, pngBlob)) {
                        thumbnailCount++;
                    } else {
                        Logger::LOG_WARN("Failed to encode PNG thumbnail for prop 0x{:08X}", prop.propID);
                    }
                    texture->Release();
                }
                resource->Release();
            }
        }

        // Save to database
        if (db.SaveProp(prop, pngBlob)) {
            savedCount++;
        } else {
            Logger::LOG_ERROR("Failed to save prop 0x{:08X} to database", prop.propID);
        }
    }

    // Set metadata
    db.SetMetadata("cache_version", "1");

    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
    localtime_s(&tm_now, &time_t_now);
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &tm_now);
    db.SetMetadata("last_build", timestamp);
    db.SetMetadata("prop_count", std::to_string(savedCount));

    if (!db.CommitTransaction()) {
        Logger::LOG_ERROR("Failed to commit transaction");
        return false;
    }

    Logger::LOG_INFO("Saved {} props ({} with thumbnails) to cache database: {}", savedCount, thumbnailCount, dbPath.string());
    return true;
}
