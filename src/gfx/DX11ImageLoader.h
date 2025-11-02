#pragma once
#include <cstdint>
#include <vector>

struct ID3D11Device;
struct ID3D11ShaderResourceView;

namespace gfx {
    // Decodes a PNG from memory using WIC and creates a D3D11 SRV with RGBA8 format.
    // Returns true on success. On success, out_srv must be released by caller.
    bool CreateSRVFromPNGMemory(const void* data, size_t size,
                                ID3D11Device* device,
                                ID3D11ShaderResourceView** out_srv,
                                int* out_width,
                                int* out_height);

    // Overload that also returns the decoded RGBA data for caching purposes.
    // If out_rgba is provided, the RGBA data will be copied into the vector.
    bool CreateSRVFromPNGMemory(const void* data, size_t size,
                                ID3D11Device* device,
                                ID3D11ShaderResourceView** out_srv,
                                int* out_width,
                                int* out_height,
                                std::vector<uint8_t>* out_rgba);
}
