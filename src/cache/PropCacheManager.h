#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

#include "../props/PropCacheEntry.h"

class cISC4City;
class cISC4PropManager;
class cIGZPersistResourceManager;
struct ID3D11Device;
struct ID3D11DeviceContext;

/**
 * @brief Manages a cache of all available props and their thumbnails
 *
 * Supports both synchronous initialization and incremental building.
 * For incremental building, use: BeginIncrementalBuild() → ProcessPropBatch() → FinalizeIncrementalBuild()
 */
class PropCacheManager {
public:
    using ProgressCallback = std::function<void(const char* stage, int current, int total)>;

    PropCacheManager();
    ~PropCacheManager();

    /**
     * @brief Initialize the cache with all available props (blocking, single-call)
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
     * @brief Begin incremental cache building (fast synchronous phase)
     * @param pCity The city instance
     * @return true if successful, false otherwise
     */
    bool BeginIncrementalBuild(cISC4City* pCity);

    /**
     * @brief Process a batch of props (call once per frame)
     * @param pRM Resource manager for loading exemplars
     * @param pDevice D3D11 device for thumbnail generation
     * @param pContext D3D11 device context
     * @param batchSize Number of props to process in this batch
     * @return Number of props actually processed
     */
    int ProcessPropBatch(
        cIGZPersistResourceManager* pRM,
        ID3D11Device* pDevice,
        ID3D11DeviceContext* pContext,
        int batchSize
    );

    /**
     * @brief Check if prop processing is complete
     */
    bool IsProcessingComplete() const;

    /**
     * @brief Get the number of props processed so far
     */
    int GetProcessedPropCount() const { return processedPropCount; }

    /**
     * @brief Get the total number of props to process
     */
    int GetTotalPropCount() const { return totalPropCount; }

    /**
     * @brief Finalize incremental building (cleanup)
     */
    void FinalizeIncrementalBuild();

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

    bool ProcessPropEntry(
        uint32_t propID,
        cIGZPersistResourceManager* pRM,
        ID3D11Device* pDevice,
        ID3D11DeviceContext* pContext
    );

    bool initialized;
    std::vector<PropCacheEntry> props;
    std::map<uint32_t, size_t> propIDToIndex;
    std::vector<uint32_t> familyTypes;
    std::vector<uint32_t> propTypesToProcess;  // For incremental building
    cISC4PropManager* pPropManager;
    ProgressCallback progressCallback;

    // Incremental build state
    int currentPropIndex = 0;
    int processedPropCount = 0;
    int totalPropCount = 0;
};
