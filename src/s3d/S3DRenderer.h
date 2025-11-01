#pragma once
#include "S3DStructures.h"
#include "S3DEnumMappings.h"
#include "FSHReader.h"
#include <d3d11.h>
#include <SimpleMath.h>
#include <CommonStates.h>
#include <unordered_map>
#include <memory>

// Forward declarations
class cISC4DBSegmentPackedFile;
class cIGZPersistResourceManager;

namespace S3D {

// Rendering constants
namespace RenderConstants {
	constexpr float BILLBOARD_ROTATION_Y = -22.5f;  // Isometric Y rotation (degrees)
	constexpr float BILLBOARD_ROTATION_X = 45.0f;   // Isometric X rotation (degrees)
	constexpr float BOUNDING_BOX_PADDING = 1.10f;   // 10% padding around model
	constexpr float NEAR_PLANE = -40000.0f;         // Near clip plane for ortho projection
	constexpr float FAR_PLANE = 40000.0f;           // Far clip plane for ortho projection
	constexpr size_t SHADER_CONSTANTS_SIZE = 256;   // Constant buffer size in bytes
}

// Debug visualization modes
enum class DebugMode {
	Normal,       // Normal rendering with textures and materials
	Wireframe,    // Wireframe overlay on top of normal rendering
	UVs,          // Visualize UV coordinates as colors (R=U, G=V)
	VertexColor,  // Show only vertex colors (no textures)
	MaterialID,   // Show material IDs as unique colors
	Normals,      // Show vertex normals as colors
	TextureOnly,  // Show textures without vertex colors
	AlphaTest     // Visualize alpha testing (red=discarded, green=kept)
};

// Shader constant buffers
struct ShaderConstants {
	DirectX::SimpleMath::Matrix viewProj;
	DirectX::SimpleMath::Vector4 padding[12]; // Pad to 256 bytes for alignment
};
static_assert(sizeof(ShaderConstants) == RenderConstants::SHADER_CONSTANTS_SIZE,
              "ShaderConstants must be 256 bytes");

struct MaterialConstants {
	float alphaThreshold;
	uint32_t alphaFunc;     // 0=NEVER, 1=LESS, 2=EQUAL, 3=LEQUAL, 4=GREATER, 5=NOTEQUAL, 6=GEQUAL, 7=ALWAYS
	uint32_t debugMode;     // Debug visualization mode (matches DebugMode enum)
	uint32_t materialIndex; // Material index for MaterialID debug mode
};

class Renderer {
public:
	Renderer(ID3D11Device* device, ID3D11DeviceContext* context);
	~Renderer();

	// Load S3D model and create GPU resources (using ResourceManager - recommended)
	bool LoadModel(const Model& model, cIGZPersistResourceManager* pRM, uint32_t groupID);

	// Load S3D model and create GPU resources (using DBPF - deprecated)
	bool LoadModelFromDBPF(const Model& model, cISC4DBSegmentPackedFile* dbpf, uint32_t groupID);

	// Clear loaded model and free GPU resources
	void ClearModel();

	// Render specific frame to current render target
	// Returns false if frame index invalid or no model loaded
	bool RenderFrame(int frameIdx = 0);

	// Generate thumbnail texture from model
	// Returns ID3D11ShaderResourceView* that can be used with ImGui::Image()
	// Size is thumbnail dimension (e.g., 128 for 128x128)
	ID3D11ShaderResourceView* GenerateThumbnail(int size = 128);

	// Check if model is loaded
	bool HasModel() const { return m_modelLoaded; }

	// Debug visualization
	void SetDebugMode(DebugMode mode) { m_debugMode = mode; }
	DebugMode GetDebugMode() const { return m_debugMode; }

private:
	struct GPUVertexBuffer {
		ID3D11Buffer* buffer = nullptr;
		uint32_t stride = 0;
		uint32_t count = 0;

		~GPUVertexBuffer() {
			if (buffer) buffer->Release();
		}
	};

	struct GPUIndexBuffer {
		ID3D11Buffer* buffer = nullptr;
		uint32_t count = 0;

		~GPUIndexBuffer() {
			if (buffer) buffer->Release();
		}
	};

	struct GPUMaterial {
		ID3D11ShaderResourceView* textureSRV = nullptr;
		ID3D11SamplerState* samplerState = nullptr;  // Per-material sampler (wrap, filter)
		ID3D11BlendState* blendState = nullptr;
		ID3D11DepthStencilState* depthState = nullptr;
		float alphaThreshold = 0.5f;
		uint32_t alphaFunc = 4; // Default: GL_GREATER
		bool hasTexture = false;

		~GPUMaterial() {
			if (textureSRV) textureSRV->Release();
			if (samplerState) samplerState->Release();
			if (blendState) blendState->Release();
			if (depthState) depthState->Release();
		}
	};

	// D3D11 resources
	ID3D11Device* m_device;
	ID3D11DeviceContext* m_context;

	// Model data
	std::vector<std::unique_ptr<GPUVertexBuffer>> m_vertexBuffers;
	std::vector<std::unique_ptr<GPUIndexBuffer>> m_indexBuffers;
	std::vector<PrimitiveBlock> m_primitiveBlocks;  // Primitive blocks from S3D file
	std::vector<std::unique_ptr<GPUMaterial>> m_materials;
	std::vector<Frame> m_frames;
	std::vector<AnimatedMesh> m_meshes;

	DirectX::SimpleMath::Vector3 m_bbMin, m_bbMax;
	bool m_modelLoaded = false;

	// Shaders
	ID3D11VertexShader* m_vertexShader = nullptr;
	ID3D11PixelShader* m_pixelShader = nullptr;
	ID3D11InputLayout* m_inputLayout = nullptr;
	ID3D11Buffer* m_constantBuffer = nullptr;  // VS constant buffer
	ID3D11Buffer* m_materialConstantBuffer = nullptr;  // PS constant buffer for material properties

	// Debug visualization
	DebugMode m_debugMode = DebugMode::Normal;
	ID3D11RasterizerState* m_wireframeRS = nullptr;  // Wireframe rasterizer state

	// DirectXTK Common States (replaces manual state creation)
	std::unique_ptr<DirectX::CommonStates> m_states;

	// Initialization
	bool CreateShaders();
	bool CreateStates();

	// Resource creation
	bool CreateVertexBuffers(const Model& model);
	bool CreateIndexBuffers(const Model& model);
	bool CreateMaterials(const Model& model, cIGZPersistResourceManager* pRM, uint32_t groupID);
	bool CreateMaterialsFromDBPF(const Model& model, cISC4DBSegmentPackedFile* dbpf, uint32_t groupID);

	// Rendering helpers
	DirectX::SimpleMath::Matrix CalculateViewProjMatrix() const;
	bool ApplyMaterial(const GPUMaterial& material, uint32_t materialIndex);

	// Offscreen rendering
	struct RenderTarget {
		ID3D11Texture2D* texture = nullptr;
		ID3D11RenderTargetView* rtv = nullptr;
		ID3D11DepthStencilView* dsv = nullptr;
		ID3D11Texture2D* depthBuffer = nullptr;
		ID3D11ShaderResourceView* srv = nullptr;
		uint32_t width = 0;
		uint32_t height = 0;

		~RenderTarget() {
			if (srv) srv->Release();
			if (dsv) dsv->Release();
			if (depthBuffer) depthBuffer->Release();
			if (rtv) rtv->Release();
			if (texture) texture->Release();
		}
	};

	std::unique_ptr<RenderTarget> CreateRenderTarget(uint32_t width, uint32_t height);
};

} // namespace S3D
