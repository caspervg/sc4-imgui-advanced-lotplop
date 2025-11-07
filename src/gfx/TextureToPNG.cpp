#include "TextureToPNG.h"
#include "utils/Logger.h"
#include <wil/com.h>
#include <wincodec.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

bool TextureToPNG::Encode(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    ID3D11Texture2D* texture,
    std::vector<uint8_t>& outPNG)
{
    if (!device || !context || !texture) {
        Logger::LOG_ERROR("Invalid parameters for PNG encoding");
        return false;
    }

    // Get texture description
    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);

    // Create staging texture for CPU readback
    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> stagingTexture;
    HRESULT hr = device->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture);
    if (FAILED(hr)) {
        Logger::LOG_ERROR("Failed to create staging texture for PNG encoding: 0x{:08X}", hr);
        return false;
    }

    // Copy texture to staging
    context->CopyResource(stagingTexture.Get(), texture);

    // Map staging texture
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = context->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        Logger::LOG_ERROR("Failed to map staging texture: 0x{:08X}", hr);
        return false;
    }

    // Copy pixel data to contiguous buffer (handle row pitch)
    // DirectX textures are typically BGRA, so we need to check and convert
    uint32_t rowPitch = desc.Width * 4; // 4 bytes per pixel
    std::vector<uint8_t> pixels(rowPitch * desc.Height);

    const uint8_t* src = static_cast<const uint8_t*>(mapped.pData);
    uint8_t* dst = pixels.data();

    // Check if we need to convert BGRA to RGBA
    bool needsSwizzle = (desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
                         desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);

    for (uint32_t row = 0; row < desc.Height; ++row) {
        if (needsSwizzle) {
            // Convert BGRA to RGBA
            for (uint32_t col = 0; col < desc.Width; ++col) {
                uint32_t srcIdx = col * 4;
                uint32_t dstIdx = col * 4;
                dst[dstIdx + 0] = src[srcIdx + 2]; // R = B
                dst[dstIdx + 1] = src[srcIdx + 1]; // G = G
                dst[dstIdx + 2] = src[srcIdx + 0]; // B = R
                dst[dstIdx + 3] = src[srcIdx + 3]; // A = A
            }
        } else {
            std::memcpy(dst, src, rowPitch);
        }
        src += mapped.RowPitch;
        dst += rowPitch;
    }

    context->Unmap(stagingTexture.Get(), 0);

    // Encode to PNG using WIC
    wil::com_ptr<IWICImagingFactory> factory;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        Logger::LOG_ERROR("Failed to create WIC factory: 0x{:08X}", hr);
        return false;
    }

    // Create growable memory stream
    ComPtr<IStream> memStream;
    hr = CreateStreamOnHGlobal(nullptr, TRUE, &memStream);
    if (FAILED(hr)) {
        Logger::LOG_ERROR("Failed to create memory stream: 0x{:08X}", hr);
        return false;
    }

    wil::com_ptr<IWICStream> stream;
    hr = factory->CreateStream(&stream);
    if (FAILED(hr)) {
        Logger::LOG_ERROR("Failed to create WIC stream: 0x{:08X}", hr);
        return false;
    }

    hr = stream->InitializeFromIStream(memStream.Get());
    if (FAILED(hr)) {
        Logger::LOG_ERROR("Failed to initialize WIC stream from IStream: 0x{:08X}", hr);
        return false;
    }

    // Create PNG encoder
    wil::com_ptr<IWICBitmapEncoder> encoder;
    hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (FAILED(hr)) {
        Logger::LOG_ERROR("Failed to create PNG encoder: 0x{:08X}", hr);
        return false;
    }

    hr = encoder->Initialize(stream.get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) {
        Logger::LOG_ERROR("Failed to initialize PNG encoder: 0x{:08X}", hr);
        return false;
    }

    // Create frame
    wil::com_ptr<IWICBitmapFrameEncode> frame;
    hr = encoder->CreateNewFrame(&frame, nullptr);
    if (FAILED(hr)) {
        Logger::LOG_ERROR("Failed to create PNG frame: 0x{:08X}", hr);
        return false;
    }

    hr = frame->Initialize(nullptr);
    if (FAILED(hr)) {
        Logger::LOG_ERROR("Failed to initialize PNG frame: 0x{:08X}", hr);
        return false;
    }

    // Set size and format
    hr = frame->SetSize(desc.Width, desc.Height);
    if (FAILED(hr)) {
        Logger::LOG_ERROR("Failed to set PNG frame size: 0x{:08X}", hr);
        return false;
    }

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppRGBA;
    hr = frame->SetPixelFormat(&format);
    if (FAILED(hr)) {
        Logger::LOG_ERROR("Failed to set PNG pixel format: 0x{:08X}", hr);
        return false;
    }

    // Write pixel data
    hr = frame->WritePixels(desc.Height, rowPitch, static_cast<UINT>(pixels.size()), pixels.data());
    if (FAILED(hr)) {
        Logger::LOG_ERROR("Failed to write PNG pixels: 0x{:08X}", hr);
        return false;
    }

    // Commit frame and encoder
    hr = frame->Commit();
    if (FAILED(hr)) {
        Logger::LOG_ERROR("Failed to commit PNG frame: 0x{:08X}", hr);
        return false;
    }

    hr = encoder->Commit();
    if (FAILED(hr)) {
        Logger::LOG_ERROR("Failed to commit PNG encoder: 0x{:08X}", hr);
        return false;
    }

    // Read encoded PNG from stream
    ULARGE_INTEGER streamSize;
    hr = stream->Seek({0}, STREAM_SEEK_END, &streamSize);
    if (FAILED(hr)) {
        Logger::LOG_ERROR("Failed to seek PNG stream: 0x{:08X}", hr);
        return false;
    }

    hr = stream->Seek({0}, STREAM_SEEK_SET, nullptr);
    if (FAILED(hr)) {
        Logger::LOG_ERROR("Failed to reset PNG stream: 0x{:08X}", hr);
        return false;
    }

    outPNG.resize(streamSize.QuadPart);
    ULONG bytesRead = 0;
    hr = stream->Read(outPNG.data(), static_cast<ULONG>(outPNG.size()), &bytesRead);
    if (FAILED(hr) || bytesRead != outPNG.size()) {
        Logger::LOG_ERROR("Failed to read PNG from stream: 0x{:08X}", hr);
        return false;
    }

    Logger::LOG_DEBUG("Encoded {}x{} texture to PNG ({} bytes)", desc.Width, desc.Height, outPNG.size());
    return true;
}
