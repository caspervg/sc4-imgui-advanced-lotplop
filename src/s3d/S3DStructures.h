#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <DirectXMath.h>

// S3D File Format Structures
// Based on: https://wiki.sc4devotion.com/index.php?title=S3D

namespace S3D {

// Vertex format - simplified to most common layout
struct Vertex {
	DirectX::XMFLOAT3 position;
	DirectX::XMFLOAT4 color;      // RGBA, default to white if not present
	DirectX::XMFLOAT2 uv;         // Primary texture coordinates
	DirectX::XMFLOAT2 uv2;        // Secondary texture coordinates (if present)
};

// Vertex buffer block
struct VertexBuffer {
	std::vector<Vertex> vertices;
	uint16_t flags;
	uint32_t format;

	// Bounding box computed from vertices
	DirectX::XMFLOAT3 bbMin;
	DirectX::XMFLOAT3 bbMax;
};

// Index buffer block
struct IndexBuffer {
	std::vector<uint16_t> indices;
	uint16_t flags;
};

// Primitive description
struct Primitive {
	uint32_t type;      // 0=triangle list, 1=triangle strip, 2=triangle fan
	uint32_t first;     // First index
	uint32_t length;    // Number of indices/vertices
};

// Primitive block (can contain multiple primitives)
using PrimitiveBlock = std::vector<Primitive>;

// Material texture description
struct MaterialTexture {
	uint32_t textureID;     // Instance ID of FSH texture
	uint8_t wrapS;          // Texture wrap mode S
	uint8_t wrapT;          // Texture wrap mode T
	uint8_t magFilter;      // Magnification filter
	uint8_t minFilter;      // Minification filter
	uint16_t animRate;
	uint16_t animMode;
	std::string animName;
};

// Material definition
struct Material {
	uint32_t flags;             // Material flags (see MAT_* defines)
	uint8_t alphaFunc;          // Alpha test function
	uint8_t depthFunc;          // Depth test function
	uint8_t srcBlend;           // Source blend mode
	uint8_t dstBlend;           // Destination blend mode
	float alphaThreshold;       // Alpha test threshold (0.0-1.0)
	uint32_t materialClass;

	std::vector<MaterialTexture> textures;
};

// Material flags
constexpr uint32_t MAT_ALPHA_TEST   = 0x01;
constexpr uint32_t MAT_DEPTH_TEST   = 0x02;
constexpr uint32_t MAT_DEPTH_WRITE  = 0x04;
constexpr uint32_t MAT_FLAT_SHADE   = 0x08;
constexpr uint32_t MAT_BLEND        = 0x10;
constexpr uint32_t MAT_TEXTURE      = 0x20;

// Animation frame - references which buffers to use
struct Frame {
	uint16_t vertBlock;
	uint16_t indexBlock;
	uint16_t primBlock;
	uint16_t matsBlock;
};

// Animated mesh
struct AnimatedMesh {
	std::string name;
	uint8_t flags;
	std::vector<Frame> frames;
};

// Animation data
struct Animation {
	uint16_t frameCount;
	uint16_t frameRate;
	uint16_t animMode;
	uint32_t flags;
	float displacement;

	std::vector<AnimatedMesh> animatedMeshes;
};

// Complete S3D model
struct Model {
	// Version
	uint16_t majorVersion;
	uint16_t minorVersion;

	// Data blocks
	std::vector<VertexBuffer> vertexBuffers;
	std::vector<IndexBuffer> indexBuffers;
	std::vector<PrimitiveBlock> primitiveBlocks;
	std::vector<Material> materials;

	// Animation
	Animation animation;

	// Overall bounding box
	DirectX::XMFLOAT3 bbMin;
	DirectX::XMFLOAT3 bbMax;

	Model() : majorVersion(0), minorVersion(0),
	          bbMin(0, 0, 0), bbMax(0, 0, 0) {}
};

} // namespace S3D
