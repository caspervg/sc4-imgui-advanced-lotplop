#pragma once
#include "S3DStructures.h"
#include "FSHReader.h"
#include <d3d11.h>
#include <DirectXMath.h>
#include <unordered_map>
#include <memory>

// Forward declarations
class cISC4DBSegmentPackedFile;
class cIGZPersistResourceManager;

namespace S3D {

// Shader constant buffers
struct ShaderConstants {
	DirectX::XMMATRIX viewProj;
	DirectX::XMFLOAT4 padding[12]; // Pad to 256 bytes for alignment
};

struct MaterialConstants {
	float alphaThreshold;
	float padding[3]; // Align to 16 bytes
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
		ID3D11BlendState* blendState = nullptr;
		ID3D11DepthStencilState* depthState = nullptr;
		float alphaThreshold = 0.5f;
		bool hasTexture = false;

		~GPUMaterial() {
			if (textureSRV) textureSRV->Release();
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
	std::vector<std::unique_ptr<GPUMaterial>> m_materials;
	std::vector<Frame> m_frames;
	std::vector<AnimatedMesh> m_meshes;

	DirectX::XMFLOAT3 m_bbMin, m_bbMax;
	bool m_modelLoaded = false;

	// Shaders
	ID3D11VertexShader* m_vertexShader = nullptr;
	ID3D11PixelShader* m_pixelShader = nullptr;
	ID3D11InputLayout* m_inputLayout = nullptr;
	ID3D11Buffer* m_constantBuffer = nullptr;  // VS constant buffer
	ID3D11Buffer* m_materialConstantBuffer = nullptr;  // PS constant buffer for material properties

	// Default states
	ID3D11SamplerState* m_samplerState = nullptr;
	ID3D11RasterizerState* m_rasterizerState = nullptr;

	// Initialization
	bool CreateShaders();
	bool CreateStates();

	// Resource creation
	bool CreateVertexBuffers(const Model& model);
	bool CreateIndexBuffers(const Model& model);
	bool CreateMaterials(const Model& model, cIGZPersistResourceManager* pRM, uint32_t groupID);
	bool CreateMaterialsFromDBPF(const Model& model, cISC4DBSegmentPackedFile* dbpf, uint32_t groupID);

	// Rendering helpers
	DirectX::XMMATRIX CalculateViewProjMatrix() const;
	bool ApplyMaterial(const GPUMaterial& material);

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
