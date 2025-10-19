#pragma once
#include <cstdint>
#include <vector>
#include <string>

// FSH (Flash) File Format Structures
// Based on: https://www.wiki.sc4devotion.com/index.php?title=FSH_Format

namespace FSH {

// FSH file magic
constexpr uint32_t MAGIC_SHPI = 0x49504853; // 'SHPI'
constexpr uint32_t MAGIC_G264 = 0x34363247; // 'G264' (FSH4)
constexpr uint32_t MAGIC_G266 = 0x36363247; // 'G266' (FSH4.1)
constexpr uint32_t MAGIC_G354 = 0x34353347; // 'G354' (FSH4.2)

// Bitmap codes (determines format)
constexpr uint8_t CODE_DXT1 = 0x60;           // DXT1 compressed (RGB, 1-bit alpha)
constexpr uint8_t CODE_DXT3 = 0x61;           // DXT3 compressed (RGBA, explicit alpha)
constexpr uint8_t CODE_32BIT = 0x7D;          // 32-bit ARGB (uncompressed)
constexpr uint8_t CODE_24BIT = 0x7F;          // 24-bit RGB (uncompressed)
constexpr uint8_t CODE_16BIT_4444 = 0x6D;    // 16-bit ARGB4444
constexpr uint8_t CODE_16BIT_0565 = 0x78;    // 16-bit RGB565
constexpr uint8_t CODE_16BIT_1555 = 0x7E;    // 16-bit ARGB1555

// Entry header structure
struct EntryHeader {
	uint32_t identifier;     // Entry name/ID (usually 0x00000000)
	uint32_t offset;         // Offset from start of file to bitmap data
};

// Directory entry (at the start of file)
struct DirectoryEntry {
	char name[4];            // Entry name (usually null)
	uint32_t offset;         // Offset to entry data
};

// Bitmap header (at each entry offset)
struct BitmapHeader {
	uint8_t code;            // Bitmap code (format type)
	uint8_t width24;         // Width bits 0-7
	uint8_t height24;        // Height bits 0-7
	uint8_t misc[3];         // Width bit 8-15, height bit 8-15, etc.

	// Decoded dimensions
	uint16_t GetWidth() const {
		return width24 | ((misc[0] & 0x0F) << 8);
	}

	uint16_t GetHeight() const {
		return height24 | ((misc[0] & 0xF0) << 4);
	}
};

// FSH file header
struct FileHeader {
	uint32_t magic;          // SHPI, G264, G266, or G354
	uint32_t size;           // File size
	uint32_t numEntries;     // Number of bitmap entries
	uint32_t dirId;          // Directory ID (usually 'GIMX' for mipmap)

	bool IsValid() const {
		return magic == MAGIC_SHPI || magic == MAGIC_G264 ||
		       magic == MAGIC_G266 || magic == MAGIC_G354;
	}

	bool HasMipmaps() const {
		return dirId == 0x584D4947; // 'GIMX'
	}
};

// Parsed bitmap entry
struct Bitmap {
	uint8_t code;            // Format code
	uint16_t width;
	uint16_t height;
	std::vector<uint8_t> data; // Raw bitmap data (compressed or uncompressed)

	// Helper methods
	bool IsDXT() const {
		return code == CODE_DXT1 || code == CODE_DXT3;
	}

	bool IsCompressed() const {
		return IsDXT();
	}

	size_t GetBytesPerPixel() const {
		switch (code) {
			case CODE_32BIT: return 4;
			case CODE_24BIT: return 3;
			case CODE_16BIT_4444:
			case CODE_16BIT_0565:
			case CODE_16BIT_1555: return 2;
			default: return 0; // Compressed formats don't have simple BPP
		}
	}

	size_t GetExpectedDataSize() const {
		if (code == CODE_DXT1) {
			return ((width + 3) / 4) * ((height + 3) / 4) * 8; // 8 bytes per 4x4 block
		} else if (code == CODE_DXT3) {
			return ((width + 3) / 4) * ((height + 3) / 4) * 16; // 16 bytes per 4x4 block
		} else {
			return width * height * GetBytesPerPixel();
		}
	}
};

// Complete FSH file
struct File {
	FileHeader header;
	std::vector<Bitmap> bitmaps; // Multiple entries (typically mipmaps)

	// Get main bitmap (highest resolution, usually first)
	const Bitmap* GetMainBitmap() const {
		return bitmaps.empty() ? nullptr : &bitmaps[0];
	}
};

} // namespace FSH
