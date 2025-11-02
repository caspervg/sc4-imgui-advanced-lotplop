#include "DX11ImageLoader.h"

#include <d3d11.h>
#include <wincodec.h>
#include <wil/com.h>
#include <vector>

namespace gfx {

static bool DecodePNGWithWIC(const void* data, size_t size, uint8_t** out_rgba, UINT* out_w, UINT* out_h)
{
    *out_rgba = nullptr;
    *out_w = *out_h = 0;

    wil::com_ptr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
        return false;

    wil::com_ptr<IWICStream> stream;
    if (FAILED(factory->CreateStream(&stream)))
        return false;

    if (FAILED(stream->InitializeFromMemory((WICInProcPointer)data, static_cast<DWORD>(size))))
        return false;

    wil::com_ptr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder)))
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

    if (FAILED(converter->Initialize(frame.get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
        return false;

    size_t stride = static_cast<size_t>(w) * 4;
    size_t bufSize = stride * static_cast<size_t>(h);
    uint8_t* pixels = (uint8_t*)malloc(bufSize);
    if (!pixels)
        return false;

    if (FAILED(converter->CopyPixels(nullptr, static_cast<UINT>(stride), static_cast<UINT>(bufSize), pixels)))
    {
        free(pixels);
        return false;
    }

    *out_rgba = pixels;
    *out_w = w;
    *out_h = h;
    return true;
}

bool CreateSRVFromPNGMemory(const void* data, size_t size,
                            ID3D11Device* device,
                            ID3D11ShaderResourceView** out_srv,
                            int* out_width,
                            int* out_height)
{
    if (!data || !size || !device || !out_srv) return false;

    uint8_t* rgba = nullptr; UINT w = 0, h = 0;
    if (!DecodePNGWithWIC(data, size, &rgba, &w, &h))
        return false;

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = w;
    texDesc.Height = h;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = rgba;
    initData.SysMemPitch = w * 4;

    wil::com_ptr<ID3D11Texture2D> texture;
    HRESULT hr = device->CreateTexture2D(&texDesc, &initData, &texture);

    free(rgba);

    if (FAILED(hr))
        return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    if (FAILED(device->CreateShaderResourceView(texture.get(), &srvDesc, out_srv)))
        return false;

    if (out_width) *out_width = static_cast<int>(w);
    if (out_height) *out_height = static_cast<int>(h);
    return true;
}

bool CreateSRVFromPNGMemory(const void* data, size_t size,
                            ID3D11Device* device,
                            ID3D11ShaderResourceView** out_srv,
                            int* out_width,
                            int* out_height,
                            std::vector<uint8_t>* out_rgba)
{
    if (!data || !size || !device || !out_srv) return false;

    uint8_t* rgba = nullptr; UINT w = 0, h = 0;
    if (!DecodePNGWithWIC(data, size, &rgba, &w, &h))
        return false;

    // Copy RGBA data to output vector if requested
    if (out_rgba) {
        size_t bufSize = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
        out_rgba->assign(rgba, rgba + bufSize);
    }

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = w;
    texDesc.Height = h;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = rgba;
    initData.SysMemPitch = w * 4;

    wil::com_ptr<ID3D11Texture2D> texture;
    HRESULT hr = device->CreateTexture2D(&texDesc, &initData, &texture);

    free(rgba);

    if (FAILED(hr))
        return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    if (FAILED(device->CreateShaderResourceView(texture.get(), &srvDesc, out_srv)))
        return false;

    if (out_width) *out_width = static_cast<int>(w);
    if (out_height) *out_height = static_cast<int>(h);
    return true;
}

} // namespace gfx
