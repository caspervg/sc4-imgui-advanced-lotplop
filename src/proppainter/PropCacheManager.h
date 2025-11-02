#pragma once
#include "PropCacheEntry.h"
#include <vector>
#include <map>
#include <functional>
#include <cstdint>

class cISC4City;
class cISC4PropManager;
class cIGZPersistResourceManager;
struct ID3D11Device;
struct ID3D11DeviceContext;

/**
 * @brief Manages a cache of all available props and their thumbnails
 */
class PropCacheManager {
public:
    using ProgressCallback = std::function<void(const char* stage, int current, int total)>;

    PropCacheManager();
    ~PropCacheManager();

    /**
     * @brief Initialize the cache with all available props
     * @param pCity The city instance
     * @param pRM Resource manager for loading exemplars
     * @param pDevice D3D11 device for thumbnail generation
     * @param pContext D3D11 device context
     * @param callback Progress callback for UI updates
     * @return true if successful
     */
    bool Initialize(
        cISC4City* pCity,
        cIGZPersistResourceManager* pRM,
        ID3D11Device* pDevice,
        ID3D11DeviceContext* pContext,
        ProgressCallback callback = nullptr
    );

    /**
     * @brief Clear the cache and release resources
     */
    void Clear();

    /**
     * @brief Check if the cache has been initialized
     */
    bool IsInitialized() const { return initialized; }

    /**
     * @brief Get all cached props
     */
    const std::vector<PropCacheEntry>& GetAllProps() const { return props; }

    /**
     * @brief Get a prop entry by ID
     */
    const PropCacheEntry* GetPropByID(uint32_t propID) const;

    /**
     * @brief Get the number of cached props
     */
    size_t GetPropCount() const { return props.size(); }

    /**
     * @brief Get all prop family types
     */
    const std::vector<uint32_t>& GetAllFamilyTypes() const { return familyTypes; }

    /**
     * @brief Get the prop manager (for family queries)
     */
    cISC4PropManager* GetPropManager() const { return pPropManager; }

private:
    bool LoadPropsFromManager(
        cISC4PropManager* pPropManager,
        cIGZPersistResourceManager* pRM,
        ID3D11Device* pDevice,
        ID3D11DeviceContext* pContext
    );

    bool initialized;
    std::vector<PropCacheEntry> props;
    std::map<uint32_t, size_t> propIDToIndex;
    std::vector<uint32_t> familyTypes;
    cISC4PropManager* pPropManager;
    ProgressCallback progressCallback;
};
