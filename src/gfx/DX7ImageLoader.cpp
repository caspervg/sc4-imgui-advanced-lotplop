#include "DX7ImageLoader.h"

#include <ddraw.h>
#include <wincodec.h>
#include <wil/com.h>
#include <cstdint>

namespace gfx {
namespace {
    bool DecodePNGWithWIC(const void* data, size_t size, uint8_t** out_rgba, UINT* out_w, UINT* out_h)
    {
        *out_rgba = nullptr;
        *out_w = *out_h = 0;

        wil::com_ptr<IWICImagingFactory> factory;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&factory))))
            return false;

        wil::com_ptr<IWICStream> stream;
        if (FAILED(factory->CreateStream(&stream)))
            return false;

        if (FAILED(stream->InitializeFromMemory(WICInProcPointer(data),
                                                static_cast<DWORD>(size))))
            return false;

        wil::com_ptr<IWICBitmapDecoder> decoder;
        if (FAILED(factory->CreateDecoderFromStream(stream.get(), nullptr,
                                                    WICDecodeMetadataCacheOnLoad, &decoder)))
            return false;

        wil::com_ptr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0, &frame)))
            return false;

        UINT w = 0, h = 0;
        if (FAILED(frame->GetSize(&w, &h)))
            return false;

        wil::com_ptr<IWICFormatConverter> converter;
        if (FAILED(factory->CreateFormatConverter(&converter)))
            return false;

        if (FAILED(converter->Initialize(frame.get(), GUID_WICPixelFormat32bppRGBA,
                                         WICBitmapDitherTypeNone, nullptr, 0.0,
                                         WICBitmapPaletteTypeCustom)))
            return false;

        size_t stride = static_cast<size_t>(w) * 4;
        size_t bufSize = stride * static_cast<size_t>(h);
        auto* pixels = static_cast<uint8_t*>(malloc(bufSize));
        if (!pixels)
            return false;

        if (FAILED(converter->CopyPixels(nullptr, static_cast<UINT>(stride),
                                         static_cast<UINT>(bufSize), pixels))) {
            free(pixels);
            return false;
        }

        *out_rgba = pixels;
        *out_w = w;
        *out_h = h;
        return true;
    }

    inline uint32_t RgbaToBgra(uint32_t c)
    {
        return (c & 0xFF00FF00u) | ((c & 0x00FF0000u) >> 16) | ((c & 0x000000FFu) << 16);
    }
}

bool CreateSurfaceFromPNGMemory(const void* data, size_t size,
                                IDirectDraw7* ddraw,
                                IDirectDrawSurface7** out_surface,
                                int* out_width,
                                int* out_height)
{
    if (!data || !size || !ddraw || !out_surface) {
        return false;
    }

    uint8_t* rgba = nullptr;
    UINT w = 0, h = 0;
    if (!DecodePNGWithWIC(data, size, &rgba, &w, &h)) {
        return false;
    }

    DDSURFACEDESC2 desc{};
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    desc.dwWidth = static_cast<DWORD>(w);
    desc.dwHeight = static_cast<DWORD>(h);
    desc.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY;

    DDPIXELFORMAT pf{};
    pf.dwSize = sizeof(pf);
    pf.dwFlags = DDPF_ALPHAPIXELS | DDPF_RGB;
    pf.dwRGBBitCount = 32;
    pf.dwRGBAlphaBitMask = 0xFF000000;
    pf.dwRBitMask = 0x00FF0000;
    pf.dwGBitMask = 0x0000FF00;
    pf.dwBBitMask = 0x000000FF;
    desc.ddpfPixelFormat = pf;

    IDirectDrawSurface7* surface = nullptr;
    if (FAILED(ddraw->CreateSurface(&desc, &surface, nullptr))) {
        desc.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_SYSTEMMEMORY;
        if (FAILED(ddraw->CreateSurface(&desc, &surface, nullptr))) {
            free(rgba);
            return false;
        }
    }

    RECT rect{0, 0, static_cast<LONG>(w), static_cast<LONG>(h)};
    DDSURFACEDESC2 lockDesc{};
    lockDesc.dwSize = sizeof(lockDesc);
    if (FAILED(surface->Lock(&rect, &lockDesc, 0, 0))) {
        surface->Release();
        free(rgba);
        return false;
    }

    const uint32_t* src = reinterpret_cast<const uint32_t*>(rgba);
    for (UINT y = 0; y < h; ++y) {
        uint32_t* dst = reinterpret_cast<uint32_t*>(
            static_cast<uint8_t*>(lockDesc.lpSurface) + y * lockDesc.lPitch);
        const uint32_t* row = src + y * w;
        for (UINT x = 0; x < w; ++x) {
            dst[x] = RgbaToBgra(row[x]);
        }
    }

    surface->Unlock(nullptr);
    free(rgba);

    *out_surface = surface;
    if (out_width) *out_width = static_cast<int>(w);
    if (out_height) *out_height = static_cast<int>(h);
    return true;
}

bool CreateSurfaceFromRGBA(const uint8_t* rgba,
                           int width,
                           int height,
                           IDirectDraw7* ddraw,
                           IDirectDrawSurface7** out_surface)
{
    if (!rgba || width <= 0 || height <= 0 || !ddraw || !out_surface) {
        return false;
    }

    DDSURFACEDESC2 desc{};
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    desc.dwWidth = static_cast<DWORD>(width);
    desc.dwHeight = static_cast<DWORD>(height);
    desc.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY;

    DDPIXELFORMAT pf{};
    pf.dwSize = sizeof(pf);
    pf.dwFlags = DDPF_ALPHAPIXELS | DDPF_RGB;
    pf.dwRGBBitCount = 32;
    pf.dwRGBAlphaBitMask = 0xFF000000;
    pf.dwRBitMask = 0x00FF0000;
    pf.dwGBitMask = 0x0000FF00;
    pf.dwBBitMask = 0x000000FF;
    desc.ddpfPixelFormat = pf;

    IDirectDrawSurface7* surface = nullptr;
    if (FAILED(ddraw->CreateSurface(&desc, &surface, nullptr))) {
        desc.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_SYSTEMMEMORY;
        if (FAILED(ddraw->CreateSurface(&desc, &surface, nullptr))) {
            return false;
        }
    }

    RECT rect{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    DDSURFACEDESC2 lockDesc{};
    lockDesc.dwSize = sizeof(lockDesc);
    if (FAILED(surface->Lock(&rect, &lockDesc, 0, 0))) {
        surface->Release();
        return false;
    }

    const uint32_t* src = reinterpret_cast<const uint32_t*>(rgba);
    for (int y = 0; y < height; ++y) {
        uint32_t* dst = reinterpret_cast<uint32_t*>(
            static_cast<uint8_t*>(lockDesc.lpSurface) + y * lockDesc.lPitch);
        const uint32_t* row = src + y * width;
        for (int x = 0; x < width; ++x) {
            dst[x] = RgbaToBgra(row[x]);
        }
    }

    surface->Unlock(nullptr);
    *out_surface = surface;
    return true;
}

} // namespace gfx
