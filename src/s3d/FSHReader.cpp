// ReSharper disable CppDFAConstantConditions
#include "FSHReader.h"
#include "QFSDecompressor.h"
#include "../utils/Logger.h"
#include "cISC4DBSegment.h"
#include "cIGZPersistResourceManager.h"
#include "cIGZPersistDBRecord.h"
#include "cGZPersistResourceKey.h"
#include "cIGZPersistDBSegment.h"
#include "cRZAutoRefCount.h"
#include <cstring>
#include <algorithm>

#include "cIGZPersistResourceKeyList.h"
#include "PersistResourceKeyFilterByTypeAndInstance.h"
#include "SC4HashSet.h"

namespace FSH {

bool Reader::Parse(const uint8_t* buffer, size_t bufferSize, File& outFile) {
	if (!buffer || bufferSize < sizeof(FileHeader)) {
		LOG_ERROR("FSH buffer too small or null");
		return false;
	}

	// Check if QFS-compressed
	std::vector<uint8_t> decompressedData;
	const uint8_t* dataPtr = buffer;
	size_t dataSize = bufferSize;

	if (QFS::Decompressor::IsQFSCompressed(buffer, bufferSize)) {
		LOG_TRACE("FSH is QFS-compressed, decompressing...");
		if (!QFS::Decompressor::Decompress(buffer, bufferSize, decompressedData)) {
			LOG_ERROR("Failed to decompress QFS-compressed FSH");
			return false;
		}
		dataPtr = decompressedData.data();
		dataSize = decompressedData.size();
	}

	const uint8_t* ptr = dataPtr;
	const uint8_t* end = dataPtr + dataSize;

	// Read header
	if (!ReadValue(ptr, end, outFile.header.magic)) return false;
	if (!ReadValue(ptr, end, outFile.header.size)) return false;
	if (!ReadValue(ptr, end, outFile.header.numEntries)) return false;
	if (!ReadValue(ptr, end, outFile.header.dirId)) return false;

	if (!outFile.header.IsValid()) {
		LOG_ERROR("Invalid FSH magic: 0x{:08X}", outFile.header.magic);
		return false;
	}

	LOG_TRACE("FSH header: magic=0x{:08X}, entries={}, hasMipmaps={}",
	          outFile.header.magic, outFile.header.numEntries, outFile.header.HasMipmaps());

	// Read directory entries
	std::vector<DirectoryEntry> directory(outFile.header.numEntries);
	for (uint32_t i = 0; i < outFile.header.numEntries; ++i) {
		if (!ReadBytes(ptr, end, directory[i].name, 4)) return false;
		if (!ReadValue(ptr, end, directory[i].offset)) return false;
	}

	// Parse each bitmap entry
	outFile.bitmaps.reserve(outFile.header.numEntries);

	for (uint32_t i = 0; i < outFile.header.numEntries; ++i) {
		// Seek to bitmap offset (use dataPtr instead of buffer to handle decompressed data)
		const uint8_t* bitmapPtr = dataPtr + directory[i].offset;
		if (bitmapPtr >= end) {
			LOG_ERROR("Invalid bitmap offset: {}", directory[i].offset);
			return false;
		}

		Bitmap bitmap;
		if (!ParseBitmap(bitmapPtr, end, bitmap)) {
			LOG_ERROR("Failed to parse bitmap {}", i);
			return false;
		}

		outFile.bitmaps.push_back(std::move(bitmap));
	}

	LOG_TRACE("Parsed {} FSH bitmaps", outFile.bitmaps.size());
	return true;
}

bool Reader::ParseBitmap(const uint8_t*& ptr, const uint8_t* end, Bitmap& outBitmap) {
	// Read bitmap header (based on FshDatIO reference implementation)
	BitmapHeader header;
	if (!ReadValue(ptr, end, header.code)) return false;     // 4 bytes
	if (!ReadValue(ptr, end, header.width)) return false;    // 2 bytes
	if (!ReadValue(ptr, end, header.height)) return false;   // 2 bytes

	// Read misc data (4 x 2 bytes)
	for (int i = 0; i < 4; ++i) {
		if (!ReadValue(ptr, end, header.misc[i])) return false;
	}

	// Extract actual format code (mask off compression flag in high byte)
	outBitmap.code = static_cast<uint8_t>(header.code & 0x7F);
	outBitmap.width = header.width;
	outBitmap.height = header.height;

	LOG_TRACE("FSH bitmap header: code=0x{:08X} (format=0x{:02X}), width={}, height={}",
	          header.code, outBitmap.code, outBitmap.width, outBitmap.height);

	// Calculate expected data size
	size_t dataSize = outBitmap.GetExpectedDataSize();

	LOG_TRACE("FSH expected data size: {} bytes (code=0x{:02X})", dataSize, outBitmap.code);

	if (dataSize == 0) {
		LOG_ERROR("Unknown FSH format code: 0x{:02X} (width={}, height={})",
		          outBitmap.code, outBitmap.width, outBitmap.height);
		return false;
	}

	// Read bitmap data
	size_t remainingBytes = end - ptr;
	if (remainingBytes < dataSize) {
		LOG_WARN("FSH bitmap data truncated: expected {}, got {}", dataSize, remainingBytes);
		dataSize = remainingBytes;
	}

	outBitmap.data.resize(dataSize);
	if (!ReadBytes(ptr, end, outBitmap.data.data(), dataSize)) return false;

	LOG_TRACE("Parsed FSH bitmap: {}x{}, code=0x{:02X}, size={}",
	          outBitmap.width, outBitmap.height, outBitmap.code, dataSize);

	return true;
}

void Reader::ARGB4444ToRGBA8(uint16_t color, uint8_t* rgba) {
	uint8_t a = (color >> 12) & 0xF;
	uint8_t r = (color >> 8) & 0xF;
	uint8_t g = (color >> 4) & 0xF;
	uint8_t b = color & 0xF;

	// Expand 4-bit to 8-bit
	rgba[0] = (r << 4) | r;
	rgba[1] = (g << 4) | g;
	rgba[2] = (b << 4) | b;
	rgba[3] = (a << 4) | a;
}

void Reader::RGB565ToRGBA8(uint16_t color, uint8_t* rgba) {
	uint8_t r = (color >> 11) & 0x1F;
	uint8_t g = (color >> 5) & 0x3F;
	uint8_t b = color & 0x1F;

	// Expand to 8-bit
	rgba[0] = (r << 3) | (r >> 2);
	rgba[1] = (g << 2) | (g >> 4);
	rgba[2] = (b << 3) | (b >> 2);
	rgba[3] = 255; // Fully opaque
}

void Reader::ARGB1555ToRGBA8(uint16_t color, uint8_t* rgba) {
	uint8_t a = (color >> 15) & 0x1;
	uint8_t r = (color >> 10) & 0x1F;
	uint8_t g = (color >> 5) & 0x1F;
	uint8_t b = color & 0x1F;

	// Expand to 8-bit
	rgba[0] = (r << 3) | (r >> 2);
	rgba[1] = (g << 3) | (g >> 2);
	rgba[2] = (b << 3) | (b >> 2);
	rgba[3] = a ? 255 : 0;
}

bool Reader::ConvertToRGBA8(const Bitmap& bitmap, std::vector<uint8_t>& outRGBA) {
	// CRITICAL: Validate against integer overflow
	if (bitmap.width == 0 || bitmap.height == 0) {
		LOG_ERROR("FSH: Invalid bitmap dimensions: {}x{}", bitmap.width, bitmap.height);
		return false;
	}

	if (bitmap.width > 65536 || bitmap.height > 65536) {
		LOG_ERROR("FSH: Bitmap dimensions too large: {}x{}", bitmap.width, bitmap.height);
		return false;
	}

	size_t pixelCount = static_cast<size_t>(bitmap.width) * static_cast<size_t>(bitmap.height);
	size_t expectedInputSize = pixelCount * bitmap.GetBytesPerPixel();

	// CRITICAL: Validate buffer size before reading
	if (bitmap.data.size() < expectedInputSize) {
		LOG_ERROR("FSH: Data buffer too small: expected {} bytes, got {} ({}x{}, format=0x{:02X})",
		          expectedInputSize, bitmap.data.size(), bitmap.width, bitmap.height, bitmap.code);
		return false;
	}

	// Check for output overflow
	size_t outputSize = pixelCount * 4;
	if (outputSize / 4 != pixelCount) { // Check for overflow in multiplication
		LOG_ERROR("FSH: Output size calculation overflow");
		return false;
	}

	outRGBA.resize(outputSize);

	switch (bitmap.code) {
		case CODE_32BIT: {
			// ARGB -> RGBA conversion
			const uint8_t* src = bitmap.data.data();
			uint8_t* dst = outRGBA.data();

			for (size_t i = 0; i < pixelCount; ++i) {
				uint8_t b = *src++;
				uint8_t g = *src++;
				uint8_t r = *src++;
				uint8_t a = *src++;

				*dst++ = r;
				*dst++ = g;
				*dst++ = b;
				*dst++ = a;
			}
			return true;
		}

		case CODE_24BIT: {
			// BGR -> RGBA conversion
			const uint8_t* src = bitmap.data.data();
			uint8_t* dst = outRGBA.data();

			for (size_t i = 0; i < pixelCount; ++i) {
				uint8_t b = *src++;
				uint8_t g = *src++;
				uint8_t r = *src++;

				*dst++ = r;
				*dst++ = g;
				*dst++ = b;
				*dst++ = 255; // Fully opaque
			}
			return true;
		}

		case CODE_16BIT_4444: {
			// Use memcpy for alignment safety instead of reinterpret_cast
			const uint8_t* src = bitmap.data.data();
			uint8_t* dst = outRGBA.data();

			for (size_t i = 0; i < pixelCount; ++i) {
				uint16_t pixel;
				std::memcpy(&pixel, src, sizeof(uint16_t));
				src += sizeof(uint16_t);
				ARGB4444ToRGBA8(pixel, dst);
				dst += 4;
			}
			return true;
		}

		case CODE_16BIT_0565: {
			// Use memcpy for alignment safety
			const uint8_t* src = bitmap.data.data();
			uint8_t* dst = outRGBA.data();

			for (size_t i = 0; i < pixelCount; ++i) {
				uint16_t pixel;
				std::memcpy(&pixel, src, sizeof(uint16_t));
				src += sizeof(uint16_t);
				RGB565ToRGBA8(pixel, dst);
				dst += 4;
			}
			return true;
		}

		case CODE_16BIT_1555: {
			// Use memcpy for alignment safety
			const uint8_t* src = bitmap.data.data();
			uint8_t* dst = outRGBA.data();

			for (size_t i = 0; i < pixelCount; ++i) {
				uint16_t pixel;
				std::memcpy(&pixel, src, sizeof(uint16_t));
				src += sizeof(uint16_t);
				ARGB1555ToRGBA8(pixel, dst);
				dst += 4;
			}
			return true;
		}

		case CODE_DXT1:
		case CODE_DXT3:
			// DXT formats are handled natively by D3D11, no conversion needed
			LOG_ERROR("DXT textures should be uploaded directly to GPU, not converted to RGBA8");
			return false;

		default:
			LOG_ERROR("Unsupported FSH format for RGBA8 conversion: 0x{:02X}", bitmap.code);
			return false;
	}
}

ID3D11ShaderResourceView* Reader::CreateTexture(
	ID3D11Device* device,
	const File& fshFile,
	bool generateMipmaps)
{
	if (!device || fshFile.bitmaps.empty()) {
		LOG_ERROR("Invalid device or empty FSH file");
		return nullptr;
	}

	const Bitmap* mainBitmap = fshFile.GetMainBitmap();
	if (!mainBitmap) {
		LOG_ERROR("No main bitmap in FSH file");
		return nullptr;
	}

	// Prepare texture description
	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.Width = mainBitmap->width;
	texDesc.Height = mainBitmap->height;
	texDesc.MipLevels = 1; // TODO: Support mipmaps from FSH
	texDesc.ArraySize = 1;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = D3D11_USAGE_IMMUTABLE;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	texDesc.CPUAccessFlags = 0;
	texDesc.MiscFlags = 0;

	// Determine D3D format and prepare data
	DXGI_FORMAT format;
	std::vector<uint8_t> textureData;
	const void* initialData = nullptr;
	UINT rowPitch = 0;

	switch (mainBitmap->code) {
		case CODE_DXT1:
			format = DXGI_FORMAT_BC1_UNORM;
			initialData = mainBitmap->data.data();
			rowPitch = ((mainBitmap->width + 3) / 4) * 8; // 8 bytes per 4x4 block
			break;

		case CODE_DXT3:
			format = DXGI_FORMAT_BC2_UNORM;
			initialData = mainBitmap->data.data();
			rowPitch = ((mainBitmap->width + 3) / 4) * 16; // 16 bytes per 4x4 block
			break;

		case CODE_32BIT:
		case CODE_24BIT:
		case CODE_16BIT_4444:
		case CODE_16BIT_0565:
		case CODE_16BIT_1555:
			// Convert to RGBA8
			format = DXGI_FORMAT_R8G8B8A8_UNORM;
			if (!ConvertToRGBA8(*mainBitmap, textureData)) {
				LOG_ERROR("Failed to convert FSH bitmap to RGBA8");
				return nullptr;
			}
			initialData = textureData.data();
			rowPitch = mainBitmap->width * 4;
			break;

		default:
			LOG_ERROR("Unsupported FSH format for D3D11 texture: 0x{:02X}", mainBitmap->code);
			return nullptr;
	}

	texDesc.Format = format;

	// Create texture
	D3D11_SUBRESOURCE_DATA initData = {};
	initData.pSysMem = initialData;
	initData.SysMemPitch = rowPitch;
	initData.SysMemSlicePitch = 0;

	ID3D11Texture2D* texture = nullptr;
	HRESULT hr = device->CreateTexture2D(&texDesc, &initData, &texture);
	if (FAILED(hr)) {
		LOG_ERROR("Failed to create D3D11 texture: 0x{:08X}", hr);
		return nullptr;
	}

	// Create shader resource view
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;

	ID3D11ShaderResourceView* srv = nullptr;
	hr = device->CreateShaderResourceView(texture, &srvDesc, &srv);
	texture->Release(); // SRV holds reference

	if (FAILED(hr)) {
		LOG_ERROR("Failed to create shader resource view: 0x{:08X}", hr);
		return nullptr;
	}

	LOG_TRACE("Created D3D11 texture from FSH: {}x{}, format=0x{:02X}",
	          mainBitmap->width, mainBitmap->height, mainBitmap->code);

	return srv;
}

bool Reader::DecompressQFS(const uint8_t* compressed, size_t compressedSize,
                           std::vector<uint8_t>& decompressed)
{
	return QFS::Decompressor::Decompress(compressed, compressedSize, decompressed);
}

ID3D11ShaderResourceView* Reader::LoadTextureFromDBPF(
	ID3D11Device* device,
	cIGZPersistDBSegment* dbpf,
	uint32_t groupID,
	uint32_t instanceID)
{
	LOG_WARN("LoadTextureFromDBPF is deprecated, use LoadTextureFromResourceManager instead");
	(void)device; (void)dbpf; (void)groupID; (void)instanceID; // Suppress unused warnings
	return nullptr;
}

ID3D11ShaderResourceView* Reader::LoadTextureFromResourceManager(
	ID3D11Device* device,
	cIGZPersistResourceManager* pRM,
	uint32_t groupID,
	uint32_t instanceID)
{
	if (!device || !pRM) {
		LOG_ERROR("Invalid device or ResourceManager");
		return nullptr;
	}

	// FSH type ID
	constexpr uint32_t FSH_TYPE_ID = 0x7AB50E44;

	// Create resource key
	cGZPersistResourceKey key(FSH_TYPE_ID, groupID, instanceID);

	// Open DB record using OpenDBRecord (same pattern as S3D loading)
	cIGZPersistDBRecord* pRecord = nullptr;
	if (!pRM->OpenDBRecord(key, &pRecord, false)) {
		LOG_DEBUG("FSH texture not found: type=0x{:08X}, group=0x{:08X}, instance=0x{:08X}. Trying all groups",
		         FSH_TYPE_ID, groupID, instanceID);

		cRZAutoRefCount<cIGZPersistResourceKeyList> pKeyList;
		pRM->GetAvailableResourceList(pKeyList.AsPPObj(), new PersistResourceKeyFilterByTypeAndInstance(FSH_TYPE_ID, instanceID));
		if (!pKeyList)
		{
			LOG_ERROR("Failed to get available resource list");
			return nullptr;
		}

		auto found = false;
		for (int i = 0; i < pKeyList->Size(); i++)
		{
			key = pKeyList->GetKey(i);
			LOG_DEBUG("Trying FSH texture in alternative group: type=0x{:08X}, group=0x{:08X}, instance=0x{:08X}",
				key.type, key.group, key.instance);
			if (pRM->OpenDBRecord(key, &pRecord, false))
			{
				LOG_DEBUG("FSH texture found in alternative group: type=0x{:08X}, group=0x{:08X}, instance=0x{:08X}",
					FSH_TYPE_ID, key.group, instanceID);
				found = true;
				break;
			}
		}

		if (!found)
		{
			LOG_WARN("FSH texture not found exhaustively either: type=0x{:08X}, group=0x{:08X}, instance=0x{:08X}",
				FSH_TYPE_ID, groupID, instanceID);
			return nullptr;
		}
	}

	cRZAutoRefCount<cIGZPersistDBRecord> record(pRecord);

	// Get data size
	uint32_t dataSize = record->GetSize();
	if (dataSize == 0) {
		LOG_ERROR("FSH record has zero size");
		return nullptr;
	}

	// Read data using GetFieldVoid
	std::vector<uint8_t> fshData(dataSize);
	if (!record->GetFieldVoid(fshData.data(), dataSize)) {
		LOG_ERROR("Failed to read FSH data from ResourceManager");
		return nullptr;
	}

	// Parse FSH
	File fshFile;
	if (!Parse(fshData.data(), dataSize, fshFile)) {
		LOG_ERROR("Failed to parse FSH file");
		return nullptr;
	}

	// Create texture
	return CreateTexture(device, fshFile, false);
}

} // namespace FSH
