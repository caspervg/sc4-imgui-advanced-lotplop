#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

struct IDirectDraw7;
struct IDirectDrawSurface7;

namespace gfx {
    // Decodes a PNG from memory using WIC and creates a DirectDraw surface.
    // Returns true on success. On success, out_surface must be Release()'d by caller.
    bool CreateSurfaceFromPNGMemory(const void* data, size_t size,
                                    IDirectDraw7* ddraw,
                                    IDirectDrawSurface7** out_surface,
                                    int* out_width,
                                    int* out_height);

    // Creates a DirectDraw surface from RGBA8 data (converted to BGRA for DDraw).
    bool CreateSurfaceFromRGBA(const uint8_t* rgba,
                               int width,
                               int height,
                               IDirectDraw7* ddraw,
                               IDirectDrawSurface7** out_surface);

    // Decodes a PNG into RGBA8 pixels. Returns false on failure.
    bool DecodePNGToRGBA(const void* data,
                         size_t size,
                         std::vector<uint8_t>& out_rgba,
                         int* out_width,
                         int* out_height);

    // Copies a DirectDraw surface into an RGBA8 buffer (BGRA -> RGBA).
    // Width/height specify the destination dimensions for stride calculation.
    bool SurfaceToRGBA(IDirectDrawSurface7* surface,
                       int width,
                       int height,
                       std::vector<uint8_t>& out_rgba);
}
