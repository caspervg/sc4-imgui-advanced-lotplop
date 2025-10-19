#include "S3DRenderer.h"
#include "S3DShaders.h"
#include "../utils/Logger.h"
#include "cISC4DBSegment.h"
#include "cIGZPersistResourceManager.h"
#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace S3D {

Renderer::Renderer(ID3D11Device* device, ID3D11DeviceContext* context)
	: m_device(device), m_context(context)
{
	if (m_device) m_device->AddRef();
	if (m_context) m_context->AddRef();

	CreateShaders();
	CreateStates();
}

Renderer::~Renderer() {
	ClearModel();

	if (m_samplerState) m_samplerState->Release();
	if (m_rasterizerState) m_rasterizerState->Release();
	if (m_materialConstantBuffer) m_materialConstantBuffer->Release();
	if (m_constantBuffer) m_constantBuffer->Release();
	if (m_inputLayout) m_inputLayout->Release();
	if (m_pixelShader) m_pixelShader->Release();
	if (m_vertexShader) m_vertexShader->Release();
	if (m_context) m_context->Release();
	if (m_device) m_device->Release();
}

bool Renderer::CreateShaders() {
	HRESULT hr;

	// Compile vertex shader
	ID3DBlob* vsBlob = nullptr;
	ID3DBlob* errorBlob = nullptr;

	hr = D3DCompile(
		Shaders::VERTEX_SHADER,
		strlen(Shaders::VERTEX_SHADER),
		"VS",
		nullptr,
		nullptr,
		"main",
		"vs_4_0",
		0,
		0,
		&vsBlob,
		&errorBlob
	);

	if (FAILED(hr)) {
		if (errorBlob) {
			LOG_ERROR("Vertex shader compilation failed: {}", (char*)errorBlob->GetBufferPointer());
			errorBlob->Release();
		}
		return false;
	}

	hr = m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_vertexShader);
	if (FAILED(hr)) {
		LOG_ERROR("Failed to create vertex shader: 0x{:08X}", hr);
		vsBlob->Release();
		return false;
	}

	// Create input layout
	D3D11_INPUT_ELEMENT_DESC layout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};

	hr = m_device->CreateInputLayout(layout, 4, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &m_inputLayout);
	vsBlob->Release();

	if (FAILED(hr)) {
		LOG_ERROR("Failed to create input layout: 0x{:08X}", hr);
		return false;
	}

	// Compile pixel shader
	ID3DBlob* psBlob = nullptr;
	hr = D3DCompile(
		Shaders::PIXEL_SHADER,
		strlen(Shaders::PIXEL_SHADER),
		"PS",
		nullptr,
		nullptr,
		"main",
		"ps_4_0",
		0,
		0,
		&psBlob,
		&errorBlob
	);

	if (FAILED(hr)) {
		if (errorBlob) {
			LOG_ERROR("Pixel shader compilation failed: {}", (char*)errorBlob->GetBufferPointer());
			errorBlob->Release();
		}
		return false;
	}

	hr = m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_pixelShader);
	psBlob->Release();

	if (FAILED(hr)) {
		LOG_ERROR("Failed to create pixel shader: 0x{:08X}", hr);
		return false;
	}

	// Create VS constant buffer
	D3D11_BUFFER_DESC cbDesc = {};
	cbDesc.ByteWidth = sizeof(ShaderConstants);
	cbDesc.Usage = D3D11_USAGE_DYNAMIC;
	cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	hr = m_device->CreateBuffer(&cbDesc, nullptr, &m_constantBuffer);
	if (FAILED(hr)) {
		LOG_ERROR("Failed to create VS constant buffer: 0x{:08X}", hr);
		return false;
	}

	// Create PS constant buffer for material properties
	cbDesc.ByteWidth = sizeof(MaterialConstants);
	hr = m_device->CreateBuffer(&cbDesc, nullptr, &m_materialConstantBuffer);
	if (FAILED(hr)) {
		LOG_ERROR("Failed to create PS constant buffer: 0x{:08X}", hr);
		return false;
	}

	LOG_DEBUG("S3D shaders created successfully");
	return true;
}

bool Renderer::CreateStates() {
	HRESULT hr;

	// Create sampler state
	D3D11_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

	hr = m_device->CreateSamplerState(&samplerDesc, &m_samplerState);
	if (FAILED(hr)) {
		LOG_ERROR("Failed to create sampler state: 0x{:08X}", hr);
		return false;
	}

	// Create rasterizer state (no culling for SC4 models)
	D3D11_RASTERIZER_DESC rastDesc = {};
	rastDesc.FillMode = D3D11_FILL_SOLID;
	rastDesc.CullMode = D3D11_CULL_NONE;
	rastDesc.FrontCounterClockwise = FALSE;
	rastDesc.DepthClipEnable = TRUE;

	hr = m_device->CreateRasterizerState(&rastDesc, &m_rasterizerState);
	if (FAILED(hr)) {
		LOG_ERROR("Failed to create rasterizer state: 0x{:08X}", hr);
		return false;
	}

	return true;
}

bool Renderer::CreateVertexBuffers(const Model& model) {
	m_vertexBuffers.clear();
	m_vertexBuffers.reserve(model.vertexBuffers.size());

	for (const auto& vb : model.vertexBuffers) {
		auto gpuVB = std::make_unique<GPUVertexBuffer>();
		gpuVB->stride = sizeof(Vertex);
		gpuVB->count = static_cast<uint32_t>(vb.vertices.size());

		D3D11_BUFFER_DESC bufferDesc = {};
		bufferDesc.ByteWidth = gpuVB->stride * gpuVB->count;
		bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
		bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

		D3D11_SUBRESOURCE_DATA initData = {};
		initData.pSysMem = vb.vertices.data();

		HRESULT hr = m_device->CreateBuffer(&bufferDesc, &initData, &gpuVB->buffer);
		if (FAILED(hr)) {
			LOG_ERROR("Failed to create vertex buffer: 0x{:08X}", hr);
			return false;
		}

		m_vertexBuffers.push_back(std::move(gpuVB));
	}

	LOG_DEBUG("Created {} vertex buffers", m_vertexBuffers.size());
	return true;
}

bool Renderer::CreateIndexBuffers(const Model& model) {
	m_indexBuffers.clear();
	m_indexBuffers.reserve(model.indexBuffers.size());

	for (const auto& ib : model.indexBuffers) {
		auto gpuIB = std::make_unique<GPUIndexBuffer>();
		gpuIB->count = static_cast<uint32_t>(ib.indices.size());

		D3D11_BUFFER_DESC bufferDesc = {};
		bufferDesc.ByteWidth = sizeof(uint16_t) * gpuIB->count;
		bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
		bufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

		D3D11_SUBRESOURCE_DATA initData = {};
		initData.pSysMem = ib.indices.data();

		HRESULT hr = m_device->CreateBuffer(&bufferDesc, &initData, &gpuIB->buffer);
		if (FAILED(hr)) {
			LOG_ERROR("Failed to create index buffer: 0x{:08X}", hr);
			return false;
		}

		m_indexBuffers.push_back(std::move(gpuIB));
	}

	LOG_DEBUG("Created {} index buffers", m_indexBuffers.size());
	return true;
}

bool Renderer::CreateMaterials(const Model& model, cIGZPersistResourceManager* pRM, uint32_t groupID) {
	m_materials.clear();
	m_materials.reserve(model.materials.size());

	for (const auto& mat : model.materials) {
		auto gpuMat = std::make_unique<GPUMaterial>();
		gpuMat->alphaThreshold = mat.alphaThreshold;
		gpuMat->hasTexture = (mat.flags & MAT_TEXTURE) && !mat.textures.empty();

		// Load texture if present
		if (gpuMat->hasTexture && pRM) {
			uint32_t textureID = mat.textures[0].textureID;

			// Try multiple common texture group IDs
			const uint32_t textureGroups[] = {
				groupID,        // Model's group ID (from S3D resource)
				0x159787AF,     // Common prop texture group
				0x1ABE787D,     // Another common prop texture group
				0x13A0BD51      // Yet another texture group
			};

			for (uint32_t tryGroup : textureGroups) {
				gpuMat->textureSRV = FSH::Reader::LoadTextureFromResourceManager(m_device, pRM, tryGroup, textureID);
				if (gpuMat->textureSRV) {
					LOG_DEBUG("Loaded texture 0x{:08X} from group 0x{:08X}", textureID, tryGroup);
					break;
				}
			}

			if (!gpuMat->textureSRV) {
				LOG_WARN("Failed to load texture 0x{:08X} for material (tried {} groups)", textureID, sizeof(textureGroups)/sizeof(textureGroups[0]));
				gpuMat->hasTexture = false; // Mark as no texture so we don't try to use it
			}
		}

		// Create blend state
		// Only enable blending if we actually have a texture loaded
		D3D11_BLEND_DESC blendDesc = {};
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		if ((mat.flags & MAT_BLEND) && gpuMat->textureSRV) {
			blendDesc.RenderTarget[0].BlendEnable = TRUE;
			blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
			blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
			blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
			blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
			blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
			blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		}

		HRESULT hr = m_device->CreateBlendState(&blendDesc, &gpuMat->blendState);
		if (FAILED(hr)) {
			LOG_ERROR("Failed to create blend state: 0x{:08X}", hr);
			return false;
		}

		// Create depth stencil state
		D3D11_DEPTH_STENCIL_DESC dsDesc = {};
		dsDesc.DepthEnable = (mat.flags & MAT_DEPTH_TEST) ? TRUE : FALSE;
		dsDesc.DepthWriteMask = (mat.flags & MAT_DEPTH_WRITE) ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
		dsDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;

		hr = m_device->CreateDepthStencilState(&dsDesc, &gpuMat->depthState);
		if (FAILED(hr)) {
			LOG_ERROR("Failed to create depth stencil state: 0x{:08X}", hr);
			return false;
		}

		m_materials.push_back(std::move(gpuMat));
	}

	LOG_DEBUG("Created {} materials", m_materials.size());
	return true;
}

bool Renderer::CreateMaterialsFromDBPF(const Model& model, cISC4DBSegmentPackedFile* dbpf, uint32_t groupID) {
	m_materials.clear();
	m_materials.reserve(model.materials.size());
	return false;
	//
	// for (const auto& mat : model.materials) {
	// 	auto gpuMat = std::make_unique<GPUMaterial>();
	// 	gpuMat->alphaThreshold = mat.alphaThreshold;
	// 	gpuMat->hasTexture = (mat.flags & MAT_TEXTURE) && !mat.textures.empty();
	//
	// 	// Load texture if present
	// 	if (gpuMat->hasTexture && dbpf) {
	// 		uint32_t textureID = mat.textures[0].textureID;
	//
	// 		// Try with model's group ID first, then fallback to special group
	// 		gpuMat->textureSRV = FSH::Reader::LoadTextureFromDBPF(m_device, dbpf, groupID, textureID);
	//
	// 		if (!gpuMat->textureSRV && groupID != 0x1ABE787D) {
	// 			// Try special prop texture group
	// 			gpuMat->textureSRV = FSH::Reader::LoadTextureFromDBPF(m_device, dbpf, 0x1ABE787D, textureID);
	// 		}
	//
	// 		if (!gpuMat->textureSRV) {
	// 			LOG_WARN("Failed to load texture 0x{:08X} for material", textureID);
	// 		}
	// 	}
	//
	// 	// Create blend state
	// 	D3D11_BLEND_DESC blendDesc = {};
	// 	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	//
	// 	if (mat.flags & MAT_BLEND) {
	// 		blendDesc.RenderTarget[0].BlendEnable = TRUE;
	// 		blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	// 		blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	// 		blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	// 		blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	// 		blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	// 		blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	// 	}
	//
	// 	HRESULT hr = m_device->CreateBlendState(&blendDesc, &gpuMat->blendState);
	// 	if (FAILED(hr)) {
	// 		LOG_ERROR("Failed to create blend state: 0x{:08X}", hr);
	// 		return false;
	// 	}
	//
	// 	// Create depth stencil state
	// 	D3D11_DEPTH_STENCIL_DESC dsDesc = {};
	// 	dsDesc.DepthEnable = (mat.flags & MAT_DEPTH_TEST) ? TRUE : FALSE;
	// 	dsDesc.DepthWriteMask = (mat.flags & MAT_DEPTH_WRITE) ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
	// 	dsDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	//
	// 	hr = m_device->CreateDepthStencilState(&dsDesc, &gpuMat->depthState);
	// 	if (FAILED(hr)) {
	// 		LOG_ERROR("Failed to create depth stencil state: 0x{:08X}", hr);
	// 		return false;
	// 	}
	//
	// 	m_materials.push_back(std::move(gpuMat));
	// }
	//
	// LOG_DEBUG("Created {} materials (from DBPF)", m_materials.size());
	// return true;
}

bool Renderer::LoadModel(const Model& model, cIGZPersistResourceManager* pRM, uint32_t groupID) {
	ClearModel();

	if (!CreateVertexBuffers(model)) return false;
	if (!CreateIndexBuffers(model)) return false;
	if (!CreateMaterials(model, pRM, groupID)) return false;

	// Copy animation data
	m_frames.clear();
	m_meshes = model.animation.animatedMeshes;

	// Extract all frames from all meshes
	for (const auto& mesh : m_meshes) {
		m_frames.insert(m_frames.end(), mesh.frames.begin(), mesh.frames.end());
	}

	m_bbMin = model.bbMin;
	m_bbMax = model.bbMax;
	m_modelLoaded = true;

	LOG_INFO("S3D model loaded: {} meshes, {} frames", m_meshes.size(), m_frames.size());
	return true;
}

bool Renderer::LoadModelFromDBPF(const Model& model, cISC4DBSegmentPackedFile* dbpf, uint32_t groupID) {
	ClearModel();

	if (!CreateVertexBuffers(model)) return false;
	if (!CreateIndexBuffers(model)) return false;
	if (!CreateMaterialsFromDBPF(model, dbpf, groupID)) return false;

	// Copy animation data
	m_frames.clear();
	m_meshes = model.animation.animatedMeshes;

	// Extract all frames from all meshes
	for (const auto& mesh : m_meshes) {
		m_frames.insert(m_frames.end(), mesh.frames.begin(), mesh.frames.end());
	}

	m_bbMin = model.bbMin;
	m_bbMax = model.bbMax;
	m_modelLoaded = true;

	LOG_INFO("S3D model loaded: {} meshes, {} frames", m_meshes.size(), m_frames.size());
	return true;
}

void Renderer::ClearModel() {
	m_vertexBuffers.clear();
	m_indexBuffers.clear();
	m_materials.clear();
	m_frames.clear();
	m_meshes.clear();
	m_modelLoaded = false;
}

DirectX::XMMATRIX Renderer::CalculateViewProjMatrix() const {
	using namespace DirectX;

	// Calculate bounding box center and size
	XMFLOAT3 center(
		(m_bbMin.x + m_bbMax.x) * 0.5f,
		(m_bbMin.y + m_bbMax.y) * 0.5f,
		(m_bbMin.z + m_bbMax.z) * 0.5f
	);

	float sizeX = m_bbMax.x - m_bbMin.x;
	float sizeY = m_bbMax.y - m_bbMin.y;
	float sizeZ = m_bbMax.z - m_bbMin.z;

	// Handle flat/2D props (like ground textures/billboards)
	if (sizeY < 0.1f) {
		sizeY = max(sizeX, sizeZ) * 0.1f; // Use 10% of horizontal size as height
	}

	float maxSize = max(sizeX, sizeY);
	maxSize = max(maxSize, sizeZ) * 1.5f; // 50% padding

	LOG_DEBUG("S3D bbox: center=({:.2f}, {:.2f}, {:.2f}), size=({:.2f}, {:.2f}, {:.2f}), maxSize={:.2f}",
	          center.x, center.y, center.z, sizeX, sizeY, sizeZ, maxSize);

	// Based on GlViewS3D.cpp reference implementation:
	// 1. Translate to center object
	// 2. Swap Y/Z axes (SC4 coordinate convention: stored Y is actually Z in world space)
	// 3. Scale by 0.01 (SC4 stores coordinates in centimeters, render in meters)
	// 4. Rotate 45° around X axis (tilt down)
	// 5. Rotate -22.5° around Y axis (slight left rotation)
	// 6. Flip Z axis for SC4's reversed Z coordinate
	// 7. Orthographic projection

	XMMATRIX model = XMMatrixIdentity();

	// Translate to center
	//model *= XMMatrixTranslation(-center.x, -center.y, -center.z);

	// Swap Y and Z axes (glVertex3f(x, z, y) in reference implementation)
	// Create axis swap matrix: X→X, Y→Z, Z→Y
	XMMATRIX axisSwap = XMMatrixIdentity();
	axisSwap.r[1] = XMVectorSet(0, 0, 1, 0);  // New Y = old Z
	axisSwap.r[2] = XMVectorSet(0, 1, 0, 0);  // New Z = old Y
	model *= axisSwap;

	// Scale from centimeters to meters (divide by 100)
	//model *= XMMatrixScaling(0.01f, 0.01f, 0.01f);

	// Rotations (in order: X then Y, matching glRotatef calls)
	model *= XMMatrixRotationX(XMConvertToRadians(45.0f));   // Tilt down
	//model *= XMMatrixRotationY(XMConvertToRadians(-22.5f));  // Rotate left

	// Flip Z axis for SC4's coordinate system
	model *= XMMatrixScaling(1.0f, 1.0f, -1.0f);

	// Orthographic projection with large depth range
	XMMATRIX proj = XMMatrixOrthographicLH(maxSize, maxSize, -40000.0f, 40000.0f);

	return model * proj;
}

bool Renderer::ApplyMaterial(const GPUMaterial& material) {
	// Set blend state
	float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	m_context->OMSetBlendState(material.blendState, blendFactor, 0xFFFFFFFF);

	// Set depth stencil state
	m_context->OMSetDepthStencilState(material.depthState, 0);

	// Set texture
	if (material.textureSRV) {
		m_context->PSSetShaderResources(0, 1, &material.textureSRV);
	} else {
		ID3D11ShaderResourceView* nullSRV = nullptr;
		m_context->PSSetShaderResources(0, 1, &nullSRV);
	}

	// Update material constant buffer with alpha threshold
	D3D11_MAPPED_SUBRESOURCE mapped;
	HRESULT hr = m_context->Map(m_materialConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	if (SUCCEEDED(hr)) {
		MaterialConstants* matConstants = static_cast<MaterialConstants*>(mapped.pData);
		matConstants->alphaThreshold = material.alphaThreshold;
		m_context->Unmap(m_materialConstantBuffer, 0);
	}

	m_context->PSSetConstantBuffers(0, 1, &m_materialConstantBuffer);

	return true;
}

bool Renderer::RenderFrame(int frameIdx) {
	if (!m_modelLoaded || m_meshes.empty()) {
		LOG_ERROR("No model loaded");
		return false;
	}

	// Setup pipeline
	m_context->IASetInputLayout(m_inputLayout);
	m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_context->VSSetShader(m_vertexShader, nullptr, 0);
	m_context->PSSetShader(m_pixelShader, nullptr, 0);
	m_context->RSSetState(m_rasterizerState);
	m_context->PSSetSamplers(0, 1, &m_samplerState);

	// Update constant buffer
	DirectX::XMMATRIX viewProj = CalculateViewProjMatrix();

	D3D11_MAPPED_SUBRESOURCE mapped;
	HRESULT hr = m_context->Map(m_constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	if (SUCCEEDED(hr)) {
		ShaderConstants* constants = static_cast<ShaderConstants*>(mapped.pData);
		constants->viewProj = DirectX::XMMatrixTranspose(viewProj);
		m_context->Unmap(m_constantBuffer, 0);
	}

	m_context->VSSetConstantBuffers(0, 1, &m_constantBuffer);

	// Render all meshes at specified frame
	int meshDrawCount = 0;
	int totalTriangles = 0;

	for (const auto& mesh : m_meshes) {
		if (frameIdx >= static_cast<int>(mesh.frames.size())) {
			LOG_WARN("Frame {} out of range for mesh {}", frameIdx, mesh.name);
			continue;
		}

		const auto& frame = mesh.frames[frameIdx];

		// Validate indices
		if (frame.vertBlock >= m_vertexBuffers.size() ||
		    frame.indexBlock >= m_indexBuffers.size() ||
		    frame.matsBlock >= m_materials.size()) {
			LOG_WARN("Invalid frame references in mesh {}", mesh.name);
			continue;
		}

		// Set buffers
		auto& vb = m_vertexBuffers[frame.vertBlock];
		auto& ib = m_indexBuffers[frame.indexBlock];
		auto& mat = m_materials[frame.matsBlock];

		UINT stride = vb->stride;
		UINT offset = 0;
		m_context->IASetVertexBuffers(0, 1, &vb->buffer, &stride, &offset);
		m_context->IASetIndexBuffer(ib->buffer, DXGI_FORMAT_R16_UINT, 0);

		// Apply material
		ApplyMaterial(*mat);

		// Draw
		m_context->DrawIndexed(ib->count, 0, 0);

		meshDrawCount++;
		totalTriangles += ib->count / 3;
	}

	LOG_DEBUG("Rendered {} meshes, {} triangles", meshDrawCount, totalTriangles);
	return true;
}

std::unique_ptr<Renderer::RenderTarget> Renderer::CreateRenderTarget(uint32_t width, uint32_t height) {
	auto rt = std::make_unique<RenderTarget>();
	rt->width = width;
	rt->height = height;

	// Create texture
	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.Width = width;
	texDesc.Height = height;
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	texDesc.SampleDesc.Count = 1;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	HRESULT hr = m_device->CreateTexture2D(&texDesc, nullptr, &rt->texture);
	if (FAILED(hr)) {
		LOG_ERROR("Failed to create render target texture: 0x{:08X}", hr);
		return nullptr;
	}

	// Create RTV
	hr = m_device->CreateRenderTargetView(rt->texture, nullptr, &rt->rtv);
	if (FAILED(hr)) {
		LOG_ERROR("Failed to create render target view: 0x{:08X}", hr);
		return nullptr;
	}

	// Create depth buffer
	D3D11_TEXTURE2D_DESC depthDesc = {};
	depthDesc.Width = width;
	depthDesc.Height = height;
	depthDesc.MipLevels = 1;
	depthDesc.ArraySize = 1;
	depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthDesc.SampleDesc.Count = 1;
	depthDesc.Usage = D3D11_USAGE_DEFAULT;
	depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

	hr = m_device->CreateTexture2D(&depthDesc, nullptr, &rt->depthBuffer);
	if (FAILED(hr)) {
		LOG_ERROR("Failed to create depth buffer: 0x{:08X}", hr);
		return nullptr;
	}

	// Create DSV
	hr = m_device->CreateDepthStencilView(rt->depthBuffer, nullptr, &rt->dsv);
	if (FAILED(hr)) {
		LOG_ERROR("Failed to create depth stencil view: 0x{:08X}", hr);
		return nullptr;
	}

	// Create SRV for ImGui
	hr = m_device->CreateShaderResourceView(rt->texture, nullptr, &rt->srv);
	if (FAILED(hr)) {
		LOG_ERROR("Failed to create shader resource view: 0x{:08X}", hr);
		return nullptr;
	}

	return rt;
}

ID3D11ShaderResourceView* Renderer::GenerateThumbnail(int size) {
	if (!m_modelLoaded) {
		LOG_ERROR("No model loaded for thumbnail generation");
		return nullptr;
	}

	// Create render target
	auto rt = CreateRenderTarget(size, size);
	if (!rt) {
		return nullptr;
	}

	// Save current render state
	ID3D11RenderTargetView* oldRTV = nullptr;
	ID3D11DepthStencilView* oldDSV = nullptr;
	m_context->OMGetRenderTargets(1, &oldRTV, &oldDSV);

	D3D11_VIEWPORT oldViewport;
	UINT numViewports = 1;
	m_context->RSGetViewports(&numViewports, &oldViewport);

	// Set our render target
	m_context->OMSetRenderTargets(1, &rt->rtv, rt->dsv);

	// Set viewport
	D3D11_VIEWPORT viewport = {};
	viewport.Width = static_cast<float>(size);
	viewport.Height = static_cast<float>(size);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	m_context->RSSetViewports(1, &viewport);

	// Clear with a visible background color for thumbnails
	float clearColor[4] = { 0.15f, 0.15f, 0.15f, 1.0f }; // Dark gray background
	m_context->ClearRenderTargetView(rt->rtv, clearColor);
	m_context->ClearDepthStencilView(rt->dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	LOG_DEBUG("Rendering to {}x{} thumbnail...", size, size);

	// Render frame 0
	RenderFrame(0);

	// Restore render state
	m_context->OMSetRenderTargets(1, &oldRTV, oldDSV);
	m_context->RSSetViewports(1, &oldViewport);

	if (oldRTV) oldRTV->Release();
	if (oldDSV) oldDSV->Release();

	// Return SRV (caller takes ownership)
	ID3D11ShaderResourceView* srv = rt->srv;
	rt->srv = nullptr; // Prevent destructor from releasing it

	LOG_DEBUG("Generated thumbnail: {}x{}", size, size);
	return srv;
}

} // namespace S3D
