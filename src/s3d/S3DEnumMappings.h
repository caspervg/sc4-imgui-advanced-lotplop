#pragma once
#include <d3d11.h>
#include <cstdint>

// OpenGL to Direct3D 11 enum mappings for S3D rendering
// S3D files use OpenGL-style enums from the original game
//
// References:
// - OpenGL 1.x specification: https://www.khronos.org/registry/OpenGL/specs/gl/glspec13.pdf
// - SimCity 4 uses OpenGL renderer on Mac, so S3D files store GL enums

namespace S3D {
namespace EnumMappings {

// OpenGL comparison functions (for alpha test, depth test)
constexpr uint8_t GL_NEVER    = 0x0200;
constexpr uint8_t GL_LESS     = 0x0201;
constexpr uint8_t GL_EQUAL    = 0x0202;
constexpr uint8_t GL_LEQUAL   = 0x0203;
constexpr uint8_t GL_GREATER  = 0x0204;
constexpr uint8_t GL_NOTEQUAL = 0x0205;
constexpr uint8_t GL_GEQUAL   = 0x0206;
constexpr uint8_t GL_ALWAYS   = 0x0207;

// OpenGL blend factors
constexpr uint8_t GL_ZERO                = 0;
constexpr uint8_t GL_ONE                 = 1;
constexpr uint8_t GL_SRC_COLOR           = 0x0300;
constexpr uint8_t GL_ONE_MINUS_SRC_COLOR = 0x0301;
constexpr uint8_t GL_SRC_ALPHA           = 0x0302;
constexpr uint8_t GL_ONE_MINUS_SRC_ALPHA = 0x0303;
constexpr uint8_t GL_DST_ALPHA           = 0x0304;
constexpr uint8_t GL_ONE_MINUS_DST_ALPHA = 0x0305;
constexpr uint8_t GL_DST_COLOR           = 0x0306;
constexpr uint8_t GL_ONE_MINUS_DST_COLOR = 0x0307;
constexpr uint8_t GL_SRC_ALPHA_SATURATE  = 0x0308;

// OpenGL texture wrap modes
constexpr uint8_t GL_REPEAT          = 0x2901;
constexpr uint8_t GL_CLAMP           = 0x2900;
constexpr uint8_t GL_CLAMP_TO_EDGE   = 0x812F;
constexpr uint8_t GL_MIRRORED_REPEAT = 0x8370;

// OpenGL texture filter modes
constexpr uint8_t GL_NEAREST                = 0x2600;
constexpr uint8_t GL_LINEAR                 = 0x2601;
constexpr uint8_t GL_NEAREST_MIPMAP_NEAREST = 0x2700;
constexpr uint8_t GL_LINEAR_MIPMAP_NEAREST  = 0x2701;
constexpr uint8_t GL_NEAREST_MIPMAP_LINEAR  = 0x2702;
constexpr uint8_t GL_LINEAR_MIPMAP_LINEAR   = 0x2703;

// Convert OpenGL comparison function to D3D11
inline D3D11_COMPARISON_FUNC MapComparisonFunc(uint8_t glFunc) {
	switch (glFunc) {
		case GL_NEVER:    return D3D11_COMPARISON_NEVER;
		case GL_LESS:     return D3D11_COMPARISON_LESS;
		case GL_EQUAL:    return D3D11_COMPARISON_EQUAL;
		case GL_LEQUAL:   return D3D11_COMPARISON_LESS_EQUAL;
		case GL_GREATER:  return D3D11_COMPARISON_GREATER;
		case GL_NOTEQUAL: return D3D11_COMPARISON_NOT_EQUAL;
		case GL_GEQUAL:   return D3D11_COMPARISON_GREATER_EQUAL;
		case GL_ALWAYS:   return D3D11_COMPARISON_ALWAYS;
		default:          return D3D11_COMPARISON_LESS_EQUAL; // Safe default
	}
}

// Convert OpenGL blend factor to D3D11
inline D3D11_BLEND MapBlendFactor(uint8_t glBlend) {
	switch (glBlend) {
		case GL_ZERO:                return D3D11_BLEND_ZERO;
		case GL_ONE:                 return D3D11_BLEND_ONE;
		case GL_SRC_COLOR:           return D3D11_BLEND_SRC_COLOR;
		case GL_ONE_MINUS_SRC_COLOR: return D3D11_BLEND_INV_SRC_COLOR;
		case GL_SRC_ALPHA:           return D3D11_BLEND_SRC_ALPHA;
		case GL_ONE_MINUS_SRC_ALPHA: return D3D11_BLEND_INV_SRC_ALPHA;
		case GL_DST_ALPHA:           return D3D11_BLEND_DEST_ALPHA;
		case GL_ONE_MINUS_DST_ALPHA: return D3D11_BLEND_INV_DEST_ALPHA;
		case GL_DST_COLOR:           return D3D11_BLEND_DEST_COLOR;
		case GL_ONE_MINUS_DST_COLOR: return D3D11_BLEND_INV_DEST_COLOR;
		case GL_SRC_ALPHA_SATURATE:  return D3D11_BLEND_SRC_ALPHA_SAT;
		default:                     return D3D11_BLEND_ONE; // Safe default
	}
}

// Convert OpenGL texture wrap mode to D3D11
inline D3D11_TEXTURE_ADDRESS_MODE MapTextureWrap(uint8_t glWrap) {
	switch (glWrap) {
		case GL_REPEAT:          return D3D11_TEXTURE_ADDRESS_WRAP;
		case GL_CLAMP:           return D3D11_TEXTURE_ADDRESS_CLAMP;
		case GL_CLAMP_TO_EDGE:   return D3D11_TEXTURE_ADDRESS_CLAMP;
		case GL_MIRRORED_REPEAT: return D3D11_TEXTURE_ADDRESS_MIRROR;
		default:                 return D3D11_TEXTURE_ADDRESS_WRAP; // Safe default
	}
}

// Convert OpenGL texture filter to D3D11 filter
// D3D11 combines min/mag/mip filters into a single enum
inline D3D11_FILTER MapTextureFilter(uint8_t glMinFilter, uint8_t glMagFilter) {
	bool minLinear = (glMinFilter == GL_LINEAR ||
	                  glMinFilter == GL_LINEAR_MIPMAP_NEAREST ||
	                  glMinFilter == GL_LINEAR_MIPMAP_LINEAR);
	bool magLinear = (glMagFilter == GL_LINEAR);
	bool mipLinear = (glMinFilter == GL_NEAREST_MIPMAP_LINEAR ||
	                  glMinFilter == GL_LINEAR_MIPMAP_LINEAR);

	// Build D3D11_FILTER from components
	if (minLinear && magLinear && mipLinear) {
		return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	} else if (minLinear && magLinear && !mipLinear) {
		return D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
	} else if (minLinear && !magLinear && mipLinear) {
		return D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
	} else if (minLinear && !magLinear && !mipLinear) {
		return D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT;
	} else if (!minLinear && magLinear && mipLinear) {
		return D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR;
	} else if (!minLinear && magLinear && !mipLinear) {
		return D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
	} else if (!minLinear && !magLinear && mipLinear) {
		return D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;
	} else {
		return D3D11_FILTER_MIN_MAG_MIP_POINT;
	}
}

// Get alpha test comparison for shader constant
// Returns: 0=NEVER, 1=LESS, 2=EQUAL, 3=LEQUAL, 4=GREATER, 5=NOTEQUAL, 6=GEQUAL, 7=ALWAYS
inline uint32_t MapAlphaFunc(uint8_t glFunc) {
	switch (glFunc) {
		case GL_NEVER:    return 0;
		case GL_LESS:     return 1;
		case GL_EQUAL:    return 2;
		case GL_LEQUAL:   return 3;
		case GL_GREATER:  return 4;
		case GL_NOTEQUAL: return 5;
		case GL_GEQUAL:   return 6;
		case GL_ALWAYS:   return 7;
		default:          return 4; // GL_GREATER (most common for alpha test)
	}
}

} // namespace EnumMappings
} // namespace S3D
