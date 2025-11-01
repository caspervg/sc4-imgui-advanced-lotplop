#pragma once
#include <cstdint>
#include <vector>

// QFS/RefPack decompression
// Based on: https://wiki.niotso.org/RefPack
// Used by SC4 to compress FSH and other files

namespace QFS {

// QFS magic numbers
constexpr uint16_t MAGIC_COMPRESSED = 0xFB10;   // Compressed data marker
constexpr uint16_t MAGIC_UNCOMPRESSED = 0x0010; // Uncompressed data marker

class Decompressor {
public:
	// Decompress QFS-compressed data
	// Returns true on success, decompressed data in 'output'
	static bool Decompress(const uint8_t* input, size_t inputSize, std::vector<uint8_t>& output);

	// Check if data is QFS-compressed
	static bool IsQFSCompressed(const uint8_t* data, size_t size);

	// Get uncompressed size from QFS header (without decompressing)
	static uint32_t GetUncompressedSize(const uint8_t* data, size_t size);

private:
	// Internal decompression implementation
	static bool DecompressInternal(const uint8_t* input, size_t inputSize,
	                               uint8_t* output, size_t outputSize);
};

} // namespace QFS
