#include "QFSDecompressor.h"
#include "../utils/Logger.h"
#include <cstring>

namespace QFS {

bool Decompressor::IsQFSCompressed(const uint8_t* data, size_t size) {
	if (!data || size < 6) return false;

	uint16_t magic = (data[0] << 8) | data[1];
	return magic == MAGIC_COMPRESSED;
}

uint32_t Decompressor::GetUncompressedSize(const uint8_t* data, size_t size) {
	if (!data || size < 6) return 0;

	uint16_t magic = (data[0] << 8) | data[1];
	if (magic != MAGIC_COMPRESSED) return 0;

	// Bytes 2-5 contain big-endian uncompressed size (24-bit)
	uint32_t uncompressedSize = (data[2] << 16) | (data[3] << 8) | data[4];
	return uncompressedSize;
}

bool Decompressor::Decompress(const uint8_t* input, size_t inputSize, std::vector<uint8_t>& output) {
	if (!input || inputSize < 6) {
		LOG_ERROR("QFS: Invalid input");
		return false;
	}

	// Check magic
	uint16_t magic = (input[0] << 8) | input[1];
	if (magic != MAGIC_COMPRESSED) {
		LOG_ERROR("QFS: Invalid magic: 0x{:04X}", magic);
		return false;
	}

	// Read uncompressed size (big-endian, 24-bit)
	uint32_t uncompressedSize = (input[2] << 16) | (input[3] << 8) | input[4];

	// Compression type (input[5])
	uint8_t compressionType = input[5];

	LOG_TRACE("QFS: Decompressing {} bytes -> {} bytes, type={}",
	          inputSize, uncompressedSize, compressionType);

	// Allocate output buffer
	output.resize(uncompressedSize);

	// Decompress (skip 6-byte header)
	const uint8_t* compressedData = input + 6;
	size_t compressedSize = inputSize - 6;

	if (!DecompressInternal(compressedData, compressedSize, output.data(), uncompressedSize)) {
		LOG_ERROR("QFS: Decompression failed");
		output.clear();
		return false;
	}

	return true;
}

bool Decompressor::DecompressInternal(const uint8_t* input, size_t inputSize,
                                      uint8_t* output, size_t outputSize)
{
	const uint8_t* inPtr = input;
	const uint8_t* inEnd = input + inputSize;
	uint8_t* outPtr = output;
	uint8_t* outEnd = output + outputSize;

	while (inPtr < inEnd && outPtr < outEnd) {
		uint8_t controlByte = *inPtr++;

		// Process 8 commands (1 bit each in control byte)
		for (int bit = 0; bit < 8 && inPtr < inEnd && outPtr < outEnd; ++bit) {
			if (controlByte & (1 << bit)) {
				// Bit set: Copy literal byte
				if (inPtr >= inEnd || outPtr >= outEnd) {
					LOG_ERROR("QFS: Unexpected end of data (literal)");
					return false;
				}
				*outPtr++ = *inPtr++;
			} else {
				// Bit clear: Copy from lookback buffer
				if (inPtr + 1 >= inEnd) {
					LOG_ERROR("QFS: Unexpected end of data (reference)");
					return false;
				}

				uint16_t reference = (inPtr[0] << 8) | inPtr[1];
				inPtr += 2;

				// Decode offset and length from reference
				int offset;
				int length;

				if (reference < 0x8000) {
					// Short reference: 15-bit offset, 3-bit length
					offset = (reference >> 3) & 0x1FFF;
					length = (reference & 0x07) + 3;

					// Check for extended length
					if (length == 10) {
						if (inPtr >= inEnd) {
							LOG_ERROR("QFS: Unexpected end of data (extended length)");
							return false;
						}
						length = *inPtr++ + 10;
					}
				} else {
					// Long reference: 13-bit offset, 3-bit length
					offset = (reference >> 3) & 0x1FFF;
					length = ((reference >> 16) & 0x07) + 4;

					// Different extended length encoding for long refs
					if (length == 11) {
						if (inPtr >= inEnd) {
							LOG_ERROR("QFS: Unexpected end of data (long extended length)");
							return false;
						}
						length = *inPtr++ + 11;
					}
				}

				// Validate offset
				if (offset == 0 || offset > (outPtr - output)) {
					LOG_ERROR("QFS: Invalid lookback offset: {}", offset);
					return false;
				}

				// Copy from lookback buffer
				const uint8_t* copySource = outPtr - offset;
				for (int i = 0; i < length && outPtr < outEnd; ++i) {
					*outPtr++ = *copySource++;
				}
			}
		}
	}

	// Check if we filled the output buffer
	if (outPtr != outEnd) {
		LOG_WARN("QFS: Output size mismatch: expected {}, got {}",
		         outputSize, outPtr - output);
	}

	return true;
}

} // namespace QFS
