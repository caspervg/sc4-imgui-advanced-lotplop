#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdint>

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
}
