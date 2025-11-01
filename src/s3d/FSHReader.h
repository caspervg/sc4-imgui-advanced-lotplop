#pragma once
#include "FSHStructures.h"
#include <d3d11.h>

// Forward declarations
class cIGZPersistDBSegment;
class cIGZPersistResourceManager;

namespace FSH {

class Reader {
public:
	// Parse FSH file from buffer (handles QFS decompression automatically)
	static bool Parse(const uint8_t* buffer, size_t bufferSize, File& outFile);

	// Load FSH from ResourceManager and create D3D11 texture (recommended)
	static ID3D11ShaderResourceView* LoadTextureFromResourceManager(
		ID3D11Device* device,
		cIGZPersistResourceManager* pRM,
		uint32_t groupID,
		uint32_t instanceID
	);

	// Load FSH from DBPF and create D3D11 texture (deprecated - use ResourceManager)
	static ID3D11ShaderResourceView* LoadTextureFromDBPF(
		ID3D11Device* device,
		cIGZPersistDBSegment* dbpf,
		uint32_t groupID,
		uint32_t instanceID
	);

	// Create D3D11 texture from FSH file (uses main bitmap, handles mipmaps)
	static ID3D11ShaderResourceView* CreateTexture(
		ID3D11Device* device,
		const File& fshFile,
		bool generateMipmaps = false
	);

	// Convert uncompressed FSH bitmap to RGBA8 format
	static bool ConvertToRGBA8(const Bitmap& bitmap, std::vector<uint8_t>& outRGBA);

private:
	// Parse individual bitmap entry
	static bool ParseBitmap(const uint8_t*& ptr, const uint8_t* end, Bitmap& outBitmap);

	// Decompress QFS-compressed data (FSH files are often QFS-compressed)
	static bool DecompressQFS(const uint8_t* compressed, size_t compressedSize,
	                          std::vector<uint8_t>& decompressed);

	// Safe memory read helpers
	template<typename T>
	static bool ReadValue(const uint8_t*& ptr, const uint8_t* end, T& value) {
		if (ptr + sizeof(T) > end) return false;
		std::memcpy(&value, ptr, sizeof(T));
		ptr += sizeof(T);
		return true;
	}

	static bool ReadBytes(const uint8_t*& ptr, const uint8_t* end, void* dest, size_t count) {
		if (ptr + count > end) return false;
		std::memcpy(dest, ptr, count);
		ptr += count;
		return true;
	}

	static bool SkipBytes(const uint8_t*& ptr, const uint8_t* end, size_t count) {
		if (ptr + count > end) return false;
		ptr += count;
		return true;
	}

	// Color conversion helpers
	static void ARGB4444ToRGBA8(uint16_t color, uint8_t* rgba);
	static void RGB565ToRGBA8(uint16_t color, uint8_t* rgba);
	static void ARGB1555ToRGBA8(uint16_t color, uint8_t* rgba);
};

} // namespace FSH
