#include "DX7ImageLoader.h"
#include <windows.h>
#include <wincodec.h>
#include <ddraw.h>
#include <wil/com.h>
#include <cstdlib>

namespace gfx {

static bool DecodePNG(const void* data, size_t size, uint8_t** out_rgba, UINT* out_w, UINT* out_h) {
    *out_rgba = nullptr; *out_w = *out_h = 0;
    wil::com_ptr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) return false;
    wil::com_ptr<IWICStream> stream; if (FAILED(factory->CreateStream(&stream))) return false;
    if (FAILED(stream->InitializeFromMemory((WICInProcPointer)data, (DWORD)size))) return false;
    wil::com_ptr<IWICBitmapDecoder> decoder; if (FAILED(factory->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder))) return false;
    wil::com_ptr<IWICBitmapFrameDecode> frame; if (FAILED(decoder->GetFrame(0, &frame))) return false;
    UINT w=0,h=0; if (FAILED(frame->GetSize(&w,&h))) return false;
    wil::com_ptr<IWICFormatConverter> converter; if (FAILED(factory->CreateFormatConverter(&converter))) return false;
    if (FAILED(converter->Initialize(frame.get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) return false;
    size_t stride = w*4; size_t bufSize = stride*h; uint8_t* pixels=(uint8_t*)malloc(bufSize); if(!pixels) return false;
    if (FAILED(converter->CopyPixels(nullptr, (UINT)stride, (UINT)bufSize, pixels))) { free(pixels); return false; }
    *out_rgba = pixels; *out_w = w; *out_h = h; return true;
}

bool CreateSurfaceFromPNGMemory(const void* data, size_t size,
                                IDirectDraw7* ddraw,
                                IDirectDrawSurface7** out_surface,
                                int* out_w,
                                int* out_h) {
    if (!data || !size || !ddraw || !out_surface) return false;
    uint8_t* rgba=nullptr; UINT w=0,h=0; if(!DecodePNG(data,size,&rgba,&w,&h)) return false;

    DDSURFACEDESC2 desc{}; desc.dwSize=sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    desc.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_SYSTEMMEMORY;
    desc.dwWidth = w; desc.dwHeight = h;
    desc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    desc.ddpfPixelFormat.dwFlags = DDPF_RGB | DDPF_ALPHAPIXELS;
    desc.ddpfPixelFormat.dwRGBBitCount = 32;
    desc.ddpfPixelFormat.dwRBitMask = 0x00ff0000;
    desc.ddpfPixelFormat.dwGBitMask = 0x0000ff00;
    desc.ddpfPixelFormat.dwBBitMask = 0x000000ff;
    desc.ddpfPixelFormat.dwRGBAlphaBitMask = 0xff000000;

    IDirectDrawSurface7* surface=nullptr; HRESULT hr = ddraw->CreateSurface(&desc,&surface,nullptr);
    if (FAILED(hr) || !surface) { free(rgba); return false; }

    RECT r{0,0,(LONG)w,(LONG)h}; DDSURFACEDESC2 lockd{}; lockd.dwSize=sizeof(lockd);
    if (FAILED(surface->Lock(&r,&lockd,0,nullptr))) { surface->Release(); free(rgba); return false; }

    for (UINT y=0;y<h;y++) {
        uint8_t* dst = (uint8_t*)lockd.lpSurface + y*lockd.lPitch;
        const uint8_t* src = rgba + y*w*4;
        // Convert RGBA -> ARGB (D3D7 expects ARGB32 ordering for ImGui backend code usage)
        for (UINT x=0;x<w;x++) {
            uint8_t r=src[x*4+0]; uint8_t g=src[x*4+1]; uint8_t b=src[x*4+2]; uint8_t a=src[x*4+3];
            dst[x*4+0] = b; // B
            dst[x*4+1] = g; // G
            dst[x*4+2] = r; // R
            dst[x*4+3] = a; // A
        }
    }
    surface->Unlock(nullptr);
    free(rgba);

    *out_surface = surface;
    if(out_w) *out_w = (int)w;
    if(out_h) *out_h = (int)h;
    return true;
}

} // namespace gfx

