#pragma once
#include <cstdint>
#include <cstddef>
struct IDirectDraw7;
struct IDirectDrawSurface7;

namespace gfx {
    // Decode PNG -> system memory RGBA and create IDirectDrawSurface7 (ARGB32) texture.
    // Returns true on success. Caller owns surface (Release when done).
    bool CreateSurfaceFromPNGMemory(const void* data, size_t size,
                                    IDirectDraw7* ddraw,
                                    IDirectDrawSurface7** out_surface,
                                    int* out_w,
                                    int* out_h);
}
