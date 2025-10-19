#pragma once
#include "S3DStructures.h"
#include <memory>

namespace S3D {

class Reader {
public:
	// Parse S3D file from buffer
	static bool Parse(const uint8_t* buffer, size_t bufferSize, Model& outModel);

private:
	// Chunk parsers - each advances the pointer
	static bool ParseHEAD(const uint8_t*& ptr, const uint8_t* end, Model& model);
	static bool ParseVERT(const uint8_t*& ptr, const uint8_t* end, Model& model);
	static bool ParseINDX(const uint8_t*& ptr, const uint8_t* end, Model& model);
	static bool ParsePRIM(const uint8_t*& ptr, const uint8_t* end, Model& model);
	static bool ParseMATS(const uint8_t*& ptr, const uint8_t* end, Model& model);
	static bool ParseANIM(const uint8_t*& ptr, const uint8_t* end, Model& model);

	// Helper to read vertex based on format
	static bool ReadVertex(const uint8_t*& ptr, const uint8_t* end,
	                       uint32_t format, uint16_t minorVersion,
	                       uint32_t stride, Vertex& outVertex);

	// Helper to decode vertex format flags (v4+)
	static void DecodeVertexFormat(uint32_t format, uint8_t& coordsNb,
	                               uint8_t& colorsNb, uint8_t& texsNb);

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

	static bool CheckMagic(const uint8_t*& ptr, const uint8_t* end, const char* expected);
};

} // namespace S3D
