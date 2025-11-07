#pragma once
#include <cstdint>
#include <vector>
#include <d3d11.h>

/**
 * @brief Encodes ID3D11Texture2D to PNG format for cache storage.
 *
 * Simple helper using Windows Imaging Component (WIC) for PNG encoding.
 * Decoding uses existing CreateSRVFromPNGMemory in DX11ImageLoader.h.
 */
class TextureToPNG {
public:
    /**
     * @brief Encodes a texture to PNG format in memory.
     *
     * @param device DirectX 11 device
     * @param context DirectX 11 device context
     * @param texture Source texture (must be GPU-readable)
     * @param outPNG Output vector to receive PNG binary data
     * @return true if successful, false on error
     */
    static bool Encode(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        ID3D11Texture2D* texture,
        std::vector<uint8_t>& outPNG);
};
