#include "S3DRenderer.h"
#include "S3DShaders.h"
#include "../utils/Logger.h"
#include "cISC4DBSegment.h"
#include "cIGZPersistResourceManager.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cfloat>

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

	// m_states uses unique_ptr and cleans up automatically
	if (m_wireframeRS) m_wireframeRS->Release();
	if (m_materialConstantBuffer) m_materialConstantBuffer->Release();
	if (m_constantBuffer) m_constantBuffer->Release();
	if (m_inputLayout) m_inputLayout->Release();
	if (m_pixelShader) m_pixelShader->Release();
	if (m_vertexShader) m_vertexShader->Release();
	if (m_context) m_context->Release();
	if (m_device) m_device->Release();
}

bool Renderer::CreateShaders() {
	LOG_TRACE("Creating S3D shaders and pipeline resources...");
	HRESULT hr;

	// Compile vertex shader
	LOG_TRACE("  Compiling vertex shader (vs_4_0, {} bytes)...", strlen(Shaders::VERTEX_SHADER));
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
			LOG_ERROR("Vertex shader compilation failed (0x{:08X}): {}", hr, (char*)errorBlob->GetBufferPointer());
			errorBlob->Release();
		} else {
			LOG_ERROR("Vertex shader compilation failed: 0x{:08X}", hr);
		}
		return false;
	}

	LOG_TRACE("    Vertex shader compiled successfully ({} bytes bytecode)", vsBlob->GetBufferSize());

	hr = m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_vertexShader);
	if (FAILED(hr)) {
		LOG_ERROR("Failed to create vertex shader object: 0x{:08X}", hr);
		vsBlob->Release();
		return false;
	}

	// Create input layout
	LOG_TRACE("  Creating input layout (4 elements: POSITION, COLOR, TEXCOORD0, TEXCOORD1)...");
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

	LOG_TRACE("    Input layout created (stride=44 bytes per vertex)");

	// Compile pixel shader
	LOG_TRACE("  Compiling pixel shader (ps_4_0, {} bytes)...", strlen(Shaders::PIXEL_SHADER));
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
			LOG_ERROR("Pixel shader compilation failed (0x{:08X}): {}", hr, (char*)errorBlob->GetBufferPointer());
			errorBlob->Release();
		} else {
			LOG_ERROR("Pixel shader compilation failed: 0x{:08X}", hr);
		}
		return false;
	}

	LOG_TRACE("    Pixel shader compiled successfully ({} bytes bytecode)", psBlob->GetBufferSize());

	hr = m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_pixelShader);
	psBlob->Release();

	if (FAILED(hr)) {
		LOG_ERROR("Failed to create pixel shader object: 0x{:08X}", hr);
		return false;
	}

	// Create VS constant buffer
	LOG_TRACE("  Creating VS constant buffer ({} bytes)...", sizeof(ShaderConstants));
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
	LOG_TRACE("  Creating PS constant buffer ({} bytes)...", sizeof(MaterialConstants));
	cbDesc.ByteWidth = sizeof(MaterialConstants);
	hr = m_device->CreateBuffer(&cbDesc, nullptr, &m_materialConstantBuffer);
	if (FAILED(hr)) {
		LOG_ERROR("Failed to create PS constant buffer: 0x{:08X}", hr);
		return false;
	}

	LOG_TRACE("S3D shaders and pipeline created successfully");
	return true;
}

bool Renderer::CreateStates() {
	// Use DirectXTK CommonStates - replaces 26 lines of manual state creation!
	m_states = std::make_unique<DirectX::CommonStates>(m_device);
	LOG_TRACE("S3D states created using DirectXTK CommonStates");

	// Create wireframe rasterizer state for debug mode
	D3D11_RASTERIZER_DESC wireframeDesc = {};
	wireframeDesc.FillMode = D3D11_FILL_WIREFRAME;
	wireframeDesc.CullMode = D3D11_CULL_NONE;
	wireframeDesc.FrontCounterClockwise = FALSE;
	wireframeDesc.DepthClipEnable = TRUE;

	HRESULT hr = m_device->CreateRasterizerState(&wireframeDesc, &m_wireframeRS);
	if (FAILED(hr)) {
		LOG_ERROR("Failed to create wireframe rasterizer state: 0x{:08X}", hr);
		return false;
	}
	LOG_TRACE("  Created wireframe rasterizer state for debug visualization");

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

	LOG_TRACE("Created {} vertex buffers", m_vertexBuffers.size());
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

	LOG_TRACE("Created {} index buffers", m_indexBuffers.size());
	return true;
}

bool Renderer::CreateMaterials(const Model& model, cIGZPersistResourceManager* pRM, uint32_t groupID) {
	m_materials.clear();
	m_materials.reserve(model.materials.size());

	LOG_TRACE("Creating {} materials for S3D model", model.materials.size());

	for (size_t matIdx = 0; matIdx < model.materials.size(); ++matIdx) {
		const auto& mat = model.materials[matIdx];
		auto gpuMat = std::make_unique<GPUMaterial>();

		LOG_TRACE("Material {}: flags=0x{:08X} (ALPHA_TEST={}, DEPTH_TEST={}, BACKFACE_CULL={}, BLEND={}, TEXTURE={}, DEPTH_WRITES={})",
			matIdx, mat.flags,
			(mat.flags & MAT_ALPHA_TEST) ? "YES" : "NO",
			(mat.flags & MAT_DEPTH_TEST) ? "YES" : "NO",
			(mat.flags & MAT_BACKFACE_CULLING) ? "YES" : "NO",
			(mat.flags & MAT_BLEND) ? "YES" : "NO",
			(mat.flags & MAT_TEXTURE) ? "YES" : "NO",
			(mat.flags & MAT_DEPTH_WRITES) ? "YES" : "NO");

		gpuMat->alphaThreshold = mat.alphaThreshold;
		gpuMat->hasTexture = (mat.flags & MAT_TEXTURE) && !mat.textures.empty();

		// Store alpha test function (if MAT_ALPHA_TEST flag is set)
		if (mat.flags & MAT_ALPHA_TEST) {
			gpuMat->alphaFunc = EnumMappings::MapAlphaFunc(mat.alphaFunc);
			LOG_TRACE("  Alpha test: func=0x{:02X} → {}, threshold={:.3f}",
				mat.alphaFunc, gpuMat->alphaFunc, mat.alphaThreshold);
		} else {
			gpuMat->alphaFunc = 7; // GL_ALWAYS - no alpha test
			LOG_TRACE("  Alpha test: disabled (ALWAYS pass)");
		}

		// Load texture if present
		if (gpuMat->hasTexture && pRM) {
			uint32_t textureID = mat.textures[0].textureID;
			LOG_TRACE("  Texture: ID=0x{:08X}, count={}", textureID, mat.textures.size());

			// Try multiple common texture group IDs
			const uint32_t textureGroups[] = {
				groupID,        // Model's group ID (from S3D resource)
				0x1abe787d		// Standard group ID for Maxis pre-rendered models + most True3D models
			};

			for (uint32_t tryGroup : textureGroups) {
				gpuMat->textureSRV = FSH::Reader::LoadTextureFromResourceManager(m_device, pRM, tryGroup, textureID);
				if (gpuMat->textureSRV) {
					LOG_TRACE("    Loaded texture 0x{:08X} from group 0x{:08X}", textureID, tryGroup);
					break;
				}
			}

			if (!gpuMat->textureSRV) {
				LOG_WARN("  Failed to load texture 0x{:08X} for material {} (tried {} groups)", textureID, matIdx, sizeof(textureGroups)/sizeof(textureGroups[0]));
				gpuMat->hasTexture = false; // Mark as no texture so we don't try to use it
			}
		}

		// Create sampler state based on texture properties
		if (gpuMat->hasTexture && !mat.textures.empty()) {
			const auto& texInfo = mat.textures[0];

			LOG_TRACE("  Texture properties: wrapS=0x{:02X}, wrapT=0x{:02X}, minFilter=0x{:02X}, magFilter=0x{:02X}",
				texInfo.wrapS, texInfo.wrapT, texInfo.minFilter, texInfo.magFilter);

			D3D11_SAMPLER_DESC samplerDesc = {};
			samplerDesc.Filter = EnumMappings::MapTextureFilter(texInfo.minFilter, texInfo.magFilter);
			samplerDesc.AddressU = EnumMappings::MapTextureWrap(texInfo.wrapS);
			samplerDesc.AddressV = EnumMappings::MapTextureWrap(texInfo.wrapT);
			samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
			samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
			samplerDesc.MinLOD = 0;
			samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

			LOG_TRACE("    Created sampler: Filter={}, AddressU={}, AddressV={}",
				int(samplerDesc.Filter), int(samplerDesc.AddressU), int(samplerDesc.AddressV));

			HRESULT hr = m_device->CreateSamplerState(&samplerDesc, &gpuMat->samplerState);
			if (FAILED(hr)) {
				LOG_ERROR("Failed to create sampler state for material {}: 0x{:08X}", matIdx, hr);
				ClearModel();
				return false;
			}
		} else if (gpuMat->hasTexture) {
			// Fallback: use CommonStates LinearClamp if no texture info
			LOG_TRACE("  Using fallback LinearClamp sampler (no texture info)");
			gpuMat->samplerState = m_states->LinearClamp();
			gpuMat->samplerState->AddRef(); // Keep reference
		}

		// Create blend state using material's blend modes
		D3D11_BLEND_DESC blendDesc = {};
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		if ((mat.flags & MAT_BLEND) && gpuMat->textureSRV) {
			blendDesc.RenderTarget[0].BlendEnable = TRUE;
			blendDesc.RenderTarget[0].SrcBlend = EnumMappings::MapBlendFactor(mat.srcBlend);
			blendDesc.RenderTarget[0].DestBlend = EnumMappings::MapBlendFactor(mat.dstBlend);
			blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
			blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
			blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
			blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;

			LOG_TRACE("  Blend: srcBlend=0x{:02X} → {}, dstBlend=0x{:02X} → {}",
				int(mat.srcBlend), int(blendDesc.RenderTarget[0].SrcBlend),
				int(mat.dstBlend), int(blendDesc.RenderTarget[0].DestBlend));
		} else {
			LOG_TRACE("  Blend: disabled");
		}

		HRESULT hr = m_device->CreateBlendState(&blendDesc, &gpuMat->blendState);
		if (FAILED(hr)) {
			LOG_ERROR("Failed to create blend state for material {}: 0x{:08X}", matIdx, hr);
			ClearModel(); // Clean up partial resources
			return false;
		}

		// Create depth stencil state using material's depth function
		D3D11_DEPTH_STENCIL_DESC dsDesc = {};
		dsDesc.DepthEnable = (mat.flags & MAT_DEPTH_TEST) ? TRUE : FALSE;
		dsDesc.DepthWriteMask = (mat.flags & MAT_DEPTH_WRITES) ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
		dsDesc.DepthFunc = EnumMappings::MapComparisonFunc(mat.depthFunc);

		LOG_TRACE("  Depth: test={}, write={}, func=0x{:02X}",
			dsDesc.DepthEnable ? "YES" : "NO",
			(dsDesc.DepthWriteMask == D3D11_DEPTH_WRITE_MASK_ALL) ? "YES" : "NO",
			mat.depthFunc);

		hr = m_device->CreateDepthStencilState(&dsDesc, &gpuMat->depthState);
		if (FAILED(hr)) {
			LOG_ERROR("Failed to create depth stencil state for material {}: 0x{:08X}", matIdx, hr);
			ClearModel(); // Clean up partial resources
			return false;
		}

		m_materials.push_back(std::move(gpuMat));
	}

	LOG_TRACE("Created {} materials successfully", m_materials.size());
	return true;
}

bool Renderer::CreateMaterialsFromDBPF(const Model& model, cISC4DBSegmentPackedFile* dbpf, uint32_t groupID) {
	// Deprecated: Use LoadModel() with ResourceManager instead
	LOG_WARN("CreateMaterialsFromDBPF is deprecated, use LoadModel with ResourceManager instead");
	(void)model; (void)dbpf; (void)groupID;
	return false;
}

bool Renderer::LoadModel(const Model& model, cIGZPersistResourceManager* pRM, uint32_t groupID) {
	ClearModel();

	LOG_INFO("Loading S3D model v{}.{} from group 0x{:08X}",
		model.majorVersion, model.minorVersion, groupID);
	LOG_TRACE("  Buffers: {} vertex, {} index, {} primitive blocks, {} materials",
		model.vertexBuffers.size(), model.indexBuffers.size(),
		model.primitiveBlocks.size(), model.materials.size());

	if (!CreateVertexBuffers(model)) return false;
	if (!CreateIndexBuffers(model)) return false;
	if (!CreateMaterials(model, pRM, groupID)) return false;

	// Copy primitive blocks
	m_primitiveBlocks = model.primitiveBlocks;

	// Log primitive block details
	if (!m_primitiveBlocks.empty()) {
		LOG_TRACE("Primitive blocks detail:");
		for (size_t i = 0; i < m_primitiveBlocks.size(); ++i) {
			const auto& block = m_primitiveBlocks[i];
			LOG_TRACE("  Block {}: {} primitives", i, block.size());
			for (size_t j = 0; j < block.size(); ++j) {
				const auto& prim = block[j];
				const char* typeStr = (prim.type == 0) ? "TRIANGLELIST" :
				                      (prim.type == 1) ? "TRIANGLESTRIP" :
				                      (prim.type == 2) ? "TRIANGLEFAN" : "UNKNOWN";
				LOG_TRACE("    Prim {}: type={} ({}), first={}, length={}",
					j, prim.type, typeStr, prim.first, prim.length);
			}
		}
	} else {
		LOG_TRACE("No primitive blocks - will use fallback rendering");
	}

	// Copy animation data
	m_frames.clear();
	m_meshes = model.animation.animatedMeshes;

	// Extract all frames from all meshes
	for (const auto& mesh : m_meshes) {
		LOG_TRACE("Mesh '{}': {} frames, flags=0x{:02X}",
			mesh.name, mesh.frames.size(), mesh.flags);
		m_frames.insert(m_frames.end(), mesh.frames.begin(), mesh.frames.end());
	}

	m_bbMin = model.bbMin;
	m_bbMax = model.bbMax;
	m_modelLoaded = true;

	LOG_INFO("S3D model loaded successfully: {} meshes, {} frames, {} primitive blocks",
		m_meshes.size(), m_frames.size(), m_primitiveBlocks.size());
	LOG_TRACE("  Bounding box: min=({:.2f}, {:.2f}, {:.2f}), max=({:.2f}, {:.2f}, {:.2f})",
		m_bbMin.x, m_bbMin.y, m_bbMin.z, m_bbMax.x, m_bbMax.y, m_bbMax.z);

	return true;
}

bool Renderer::LoadModelFromDBPF(const Model& model, cISC4DBSegmentPackedFile* dbpf, uint32_t groupID) {
	ClearModel();

	if (!CreateVertexBuffers(model)) return false;
	if (!CreateIndexBuffers(model)) return false;
	if (!CreateMaterialsFromDBPF(model, dbpf, groupID)) return false;

	// Copy primitive blocks
	m_primitiveBlocks = model.primitiveBlocks;

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

	LOG_INFO("S3D model loaded: {} meshes, {} frames, {} primitive blocks",
		m_meshes.size(), m_frames.size(), m_primitiveBlocks.size());
	return true;
}

void Renderer::ClearModel() {
	m_vertexBuffers.clear();
	m_indexBuffers.clear();
	m_primitiveBlocks.clear();
	m_materials.clear();
	m_frames.clear();
	m_meshes.clear();
	m_modelLoaded = false;
}

DirectX::SimpleMath::Matrix Renderer::CalculateViewProjMatrix() const
{
	using namespace DirectX;
	using namespace DirectX::SimpleMath;

	LOG_TRACE("Calculating view-projection matrix for S3D rendering...");
	LOG_TRACE("  Model bounding box: min=({:.3f}, {:.3f}, {:.3f}), max=({:.3f}, {:.3f}, {:.3f})",
		m_bbMin.x, m_bbMin.y, m_bbMin.z, m_bbMax.x, m_bbMax.y, m_bbMax.z);

	const float ry_deg = RenderConstants::BILLBOARD_ROTATION_Y;  // -22.5° isometric Y rotation
	const float rx_deg = RenderConstants::BILLBOARD_ROTATION_X;  // 45° isometric X tilt

	LOG_TRACE("  Billboard rotation: Y={:.1f}°, X={:.1f}°", ry_deg, rx_deg);

	Matrix rotY_pos = Matrix::CreateRotationY(XMConvertToRadians(22.5f));   // +22.5 for bounds calculation
	Matrix rotX_neg = Matrix::CreateRotationX(XMConvertToRadians(-45.0f));  // -45 for bounds calculation

	const float minx = m_bbMin.x, miny = m_bbMin.y, minz = m_bbMin.z;
	const float maxx = m_bbMax.x, maxy = m_bbMax.y, maxz = m_bbMax.z;

	Vector3 corners[8] = {
		Vector3(minx, miny, minz), Vector3(maxx, miny, minz),
		Vector3(minx, maxy, minz), Vector3(maxx, maxy, minz),
		Vector3(minx, miny, maxz), Vector3(maxx, miny, maxz),
		Vector3(minx, maxy, maxz), Vector3(maxx, maxy, maxz)
	};

	float minX = FLT_MAX, minY = FLT_MAX;
	float maxX = -FLT_MAX, maxY = -FLT_MAX;
	float maxZ = -FLT_MAX;
	for (int i = 0; i < 8; ++i) {
		Vector3 v = Vector3::Transform(corners[i], rotY_pos);
		v = Vector3::Transform(v, rotX_neg);
		minX = (std::min)(minX, v.x); maxX = (std::max)(maxX, v.x);
		minY = (std::min)(minY, v.y); maxY = (std::max)(maxY, v.y);
		maxZ = (std::max)(maxZ, v.z);
	}
	LOG_TRACE("  Rotated bounds: X=[{:.3f}, {:.3f}], Y=[{:.3f}, {:.3f}], maxZ={:.3f}",
		minX, maxX, minY, maxY, maxZ);

	const float padding = RenderConstants::BOUNDING_BOX_PADDING;
	float width = (maxX - minX);
	float height = (maxY - minY);
	float diff = (std::max)(width, height) * padding;
	if (diff < 1e-4f) {
		LOG_WARN("  Model size too small ({:.6f}), clamping to 1.0", diff);
		diff = 1.0f;
	}

	float posx = (minX + maxX) * 0.5f;
	float posy = (minY + maxY) * 0.5f;
	float posz = maxZ;

	LOG_TRACE("  Orthographic projection: size={:.3f} (width={:.3f}, height={:.3f}, padding={:.0f}%)",
		diff, width, height, (padding - 1.0f) * 100.0f);
	LOG_TRACE("  View center: ({:.3f}, {:.3f}, {:.3f})", posx, posy, posz);

	// DirectX applies transformations left-to-right, OpenGL applies right-to-left
	// To match Python OpenGL: RotateY -> RotateX -> Translate
	LOG_TRACE("  Building view matrix (billboard: rotateY, rotateX, translate)...");
	Matrix view = Matrix::Identity;
	view *= Matrix::CreateRotationY(XMConvertToRadians(ry_deg));       // Rotate -22.5° (applied first to vertices)
	view *= Matrix::CreateRotationX(XMConvertToRadians(rx_deg));       // Tilt down 45° (applied second)
	view *= Matrix::CreateTranslation(-posx, -posy, -posz);            // Center model (applied last)

	// Python OpenGL uses glOrtho(..., 40000, -40000) which creates a projection where
	// the near plane is at -40000 and far at 40000 along the Z axis (after view transform)
	// In DirectX LH, we need near < far, so we use symmetric range around 0
	LOG_TRACE("  Building projection matrix (orthographic LH, near={:.1f}, far={:.1f})...",
		RenderConstants::NEAR_PLANE, RenderConstants::FAR_PLANE);
	Matrix proj = Matrix(XMMatrixOrthographicLH(diff, diff,
		RenderConstants::NEAR_PLANE, RenderConstants::FAR_PLANE));
	Matrix viewProj = view * proj;

	LOG_TRACE("  ViewProj matrix computed:");
	LOG_TRACE("    [{:7.3f} {:7.3f} {:7.3f} {:7.3f}]",
		viewProj._11, viewProj._12, viewProj._13, viewProj._14);
	LOG_TRACE("    [{:7.3f} {:7.3f} {:7.3f} {:7.3f}]",
		viewProj._21, viewProj._22, viewProj._23, viewProj._24);
	LOG_TRACE("    [{:7.3f} {:7.3f} {:7.3f} {:7.3f}]",
		viewProj._31, viewProj._32, viewProj._33, viewProj._34);
	LOG_TRACE("    [{:7.3f} {:7.3f} {:7.3f} {:7.3f}]",
		viewProj._41, viewProj._42, viewProj._43, viewProj._44);

	return viewProj;
}

bool Renderer::ApplyMaterial(const GPUMaterial& material, uint32_t materialIndex) {
	// Set blend state
	float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	m_context->OMSetBlendState(material.blendState, blendFactor, 0xFFFFFFFF);

	// Set depth stencil state
	m_context->OMSetDepthStencilState(material.depthState, 0);

	// Set texture and sampler
	if (material.textureSRV) {
		m_context->PSSetShaderResources(0, 1, &material.textureSRV);
		LOG_TRACE("      Texture: bound, sampler={}", material.samplerState ? "custom" : "none");

		// Use per-material sampler (wrapping, filtering)
		if (material.samplerState) {
			m_context->PSSetSamplers(0, 1, &material.samplerState);
		}
	} else {
		ID3D11ShaderResourceView* nullSRV = nullptr;
		m_context->PSSetShaderResources(0, 1, &nullSRV);
		LOG_TRACE("      Texture: none");
	}

	// Update material constant buffer with alpha threshold, function, debug mode, and material index
	D3D11_MAPPED_SUBRESOURCE mapped;
	HRESULT hr = m_context->Map(m_materialConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	if (SUCCEEDED(hr)) {
		MaterialConstants* matConstants = static_cast<MaterialConstants*>(mapped.pData);
		matConstants->alphaThreshold = material.alphaThreshold;
		matConstants->alphaFunc = material.alphaFunc;
		matConstants->debugMode = static_cast<uint32_t>(m_debugMode);
		matConstants->materialIndex = materialIndex;
		m_context->Unmap(m_materialConstantBuffer, 0);

		LOG_TRACE("      Alpha: threshold={:.3f}, func={}, debugMode={}, matIdx={}",
			material.alphaThreshold, material.alphaFunc, static_cast<int>(m_debugMode), materialIndex);
	} else {
		LOG_WARN("      Failed to map material constant buffer: 0x{:08X}", hr);
	}

	m_context->PSSetConstantBuffers(0, 1, &m_materialConstantBuffer);

	return true;
}

bool Renderer::RenderFrame(int frameIdx) {
	if (!m_modelLoaded || m_meshes.empty()) {
		LOG_ERROR("RenderFrame: No model loaded");
		return false;
	}

	LOG_TRACE("RenderFrame: Rendering frame {} of {} meshes", frameIdx, m_meshes.size());

	// Setup pipeline
	m_context->IASetInputLayout(m_inputLayout);
	m_context->VSSetShader(m_vertexShader, nullptr, 0);
	m_context->PSSetShader(m_pixelShader, nullptr, 0);

	// Set rasterizer state based on debug mode
	if (m_debugMode == DebugMode::Wireframe) {
		m_context->RSSetState(m_wireframeRS);
		LOG_TRACE("  Rasterizer: wireframe (debug mode)");
	} else {
		m_context->RSSetState(m_states->CullNone());
		LOG_TRACE("  Rasterizer: solid");
	}

	// Update constant buffer
	DirectX::SimpleMath::Matrix viewProj = CalculateViewProjMatrix();

	D3D11_MAPPED_SUBRESOURCE mapped;
	HRESULT hr = m_context->Map(m_constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	if (SUCCEEDED(hr)) {
		ShaderConstants* constants = static_cast<ShaderConstants*>(mapped.pData);
		// No transpose needed - using column-vector multiplication in shader
		constants->viewProj = viewProj;
		m_context->Unmap(m_constantBuffer, 0);
	} else {
		LOG_ERROR("Failed to map VS constant buffer: 0x{:08X}", hr);
		return false;
	}

	m_context->VSSetConstantBuffers(0, 1, &m_constantBuffer);

	// Render all meshes at specified frame
	int meshDrawCount = 0;
	int totalTriangles = 0;
	int totalDrawCalls = 0;

	for (size_t meshIdx = 0; meshIdx < m_meshes.size(); ++meshIdx) {
		const auto& mesh = m_meshes[meshIdx];

		if (frameIdx >= static_cast<int>(mesh.frames.size())) {
			LOG_WARN("  Mesh {}: Frame {} out of range (has {} frames)", mesh.name, frameIdx, mesh.frames.size());
			continue;
		}

		const auto& frame = mesh.frames[frameIdx];

		LOG_TRACE("  Mesh {} '{}': vert={}, index={}, prim={}, mat={}",
			meshIdx, mesh.name, frame.vertBlock, frame.indexBlock, frame.primBlock, frame.matsBlock);

		// Validate indices
		if (frame.vertBlock >= m_vertexBuffers.size() ||
		    frame.indexBlock >= m_indexBuffers.size() ||
		    frame.matsBlock >= m_materials.size()) {
			LOG_ERROR("    Invalid frame references: vert={}/{}, index={}/{}, mat={}/{}",
				frame.vertBlock, m_vertexBuffers.size(),
				frame.indexBlock, m_indexBuffers.size(),
				frame.matsBlock, m_materials.size());
			continue;
		}

		// Set buffers
		auto& vb = m_vertexBuffers[frame.vertBlock];
		auto& ib = m_indexBuffers[frame.indexBlock];
		auto& mat = m_materials[frame.matsBlock];

		LOG_TRACE("    Buffers: VB={} verts, IB={} indices",
			vb->count, ib->count);

		UINT stride = vb->stride;
		UINT offset = 0;
		m_context->IASetVertexBuffers(0, 1, &vb->buffer, &stride, &offset);
		m_context->IASetIndexBuffer(ib->buffer, DXGI_FORMAT_R16_UINT, 0);

		// Apply material (sets blend state, depth state, texture, sampler, debug mode)
		LOG_TRACE("    Material {}:", frame.matsBlock);
		ApplyMaterial(*mat, frame.matsBlock);

		// Draw using primitive blocks if available
		if (frame.primBlock < m_primitiveBlocks.size() && !m_primitiveBlocks[frame.primBlock].empty()) {
			const auto& primBlock = m_primitiveBlocks[frame.primBlock];

			LOG_TRACE("    Using primitive block {} ({} primitives)", frame.primBlock, primBlock.size());

			for (size_t primIdx = 0; primIdx < primBlock.size(); ++primIdx) {
				const auto& prim = primBlock[primIdx];

				// Set primitive topology based on type
				D3D11_PRIMITIVE_TOPOLOGY topology;
				const char* topologyStr;
				switch (prim.type) {
					case 0:
						topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
						topologyStr = "TRIANGLELIST";
						break;
					case 1:
						topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
						topologyStr = "TRIANGLESTRIP";
						break;
					case 2:
						// Triangle fan not supported in D3D11
						LOG_WARN("      Prim {}: TRIANGLEFAN not supported, skipping", primIdx);
						continue;
					default:
						LOG_WARN("      Prim {}: Unknown type {}, skipping", primIdx, prim.type);
						continue;
				}

				m_context->IASetPrimitiveTopology(topology);

				// Draw primitive
				LOG_TRACE("      Prim {}: {} first={}, length={}",
					primIdx, topologyStr, prim.first, prim.length);
				m_context->DrawIndexed(prim.length, prim.first, 0);
				totalDrawCalls++;

				// Count triangles (approximation for strips)
				if (prim.type == 0) {
					totalTriangles += prim.length / 3;
				} else if (prim.type == 1) {
					totalTriangles += (prim.length > 2) ? (prim.length - 2) : 0;
				}
			}
		} else {
			// Fallback: No primitive blocks, draw entire index buffer as triangle list
			LOG_TRACE("    Using fallback rendering (no primitive blocks)");
			m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			m_context->DrawIndexed(ib->count, 0, 0);
			totalDrawCalls++;
			totalTriangles += ib->count / 3;
		}

		meshDrawCount++;
	}

	LOG_TRACE("RenderFrame complete: {} meshes, {} draw calls, {} triangles",
		meshDrawCount, totalDrawCalls, totalTriangles);
	return true;
}

std::unique_ptr<Renderer::RenderTarget> Renderer::CreateRenderTarget(uint32_t width, uint32_t height) {
	LOG_TRACE("Creating render target: {}x{}", width, height);
	auto rt = std::make_unique<RenderTarget>();
	rt->width = width;
	rt->height = height;

	// Create texture
	LOG_TRACE("  Creating color texture (RGBA8, {}x{})...", width, height);
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
		LOG_ERROR("Failed to create render target texture ({}x{}): 0x{:08X}", width, height, hr);
		return nullptr;
	}

	// Create RTV
	LOG_TRACE("  Creating render target view...");
	hr = m_device->CreateRenderTargetView(rt->texture, nullptr, &rt->rtv);
	if (FAILED(hr)) {
		LOG_ERROR("Failed to create render target view: 0x{:08X}", hr);
		return nullptr;
	}

	// Create depth buffer
	LOG_TRACE("  Creating depth buffer (D24S8, {}x{})...", width, height);
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
	LOG_TRACE("  Creating depth stencil view...");
	hr = m_device->CreateDepthStencilView(rt->depthBuffer, nullptr, &rt->dsv);
	if (FAILED(hr)) {
		LOG_ERROR("Failed to create depth stencil view: 0x{:08X}", hr);
		return nullptr;
	}

	// Create SRV for ImGui
	LOG_TRACE("  Creating shader resource view (for ImGui display)...");
	hr = m_device->CreateShaderResourceView(rt->texture, nullptr, &rt->srv);
	if (FAILED(hr)) {
		LOG_ERROR("Failed to create shader resource view: 0x{:08X}", hr);
		return nullptr;
	}

	LOG_TRACE("Render target created successfully");
	return rt;
}

ID3D11ShaderResourceView* Renderer::GenerateThumbnail(int size) {
	LOG_INFO("Generating S3D thumbnail: {}x{}", size, size);

	if (!m_modelLoaded) {
		LOG_ERROR("GenerateThumbnail: No model loaded");
		return nullptr;
	}

	// Create render target
	LOG_TRACE("  Creating offscreen render target...");
	auto rt = CreateRenderTarget(size, size);
	if (!rt) {
		LOG_ERROR("  Failed to create render target for thumbnail");
		return nullptr;
	}

	// Save current render state (comprehensive state capture)
	LOG_TRACE("  Saving current GPU render state...");
	ID3D11RenderTargetView* oldRTV = nullptr;
	ID3D11DepthStencilView* oldDSV = nullptr;
	m_context->OMGetRenderTargets(1, &oldRTV, &oldDSV);

	D3D11_VIEWPORT oldViewport = {};
	UINT numViewports = 1;
	m_context->RSGetViewports(&numViewports, &oldViewport);

	// Save rasterizer state (for ImGui)
	ID3D11RasterizerState* oldRS = nullptr;
	m_context->RSGetState(&oldRS);

	// Save blend state (for ImGui)
	ID3D11BlendState* oldBS = nullptr;
	FLOAT oldBlendFactor[4];
	UINT oldSampleMask;
	m_context->OMGetBlendState(&oldBS, oldBlendFactor, &oldSampleMask);

	// Save depth stencil state (for ImGui)
	ID3D11DepthStencilState* oldDSS = nullptr;
	UINT oldStencilRef;
	m_context->OMGetDepthStencilState(&oldDSS, &oldStencilRef);

	LOG_TRACE("    Saved: RTV={}, DSV={}, RS={}, BS={}, DSS={}",
		oldRTV ? "yes" : "no", oldDSV ? "yes" : "no",
		oldRS ? "yes" : "no", oldBS ? "yes" : "no", oldDSS ? "yes" : "no");

	// Set our render target
	LOG_TRACE("  Setting thumbnail render target and viewport...");
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
	LOG_TRACE("  Clearing render target (background: rgb({:.0f}, {:.0f}, {:.0f}))...",
		clearColor[0] * 255, clearColor[1] * 255, clearColor[2] * 255);
	m_context->ClearRenderTargetView(rt->rtv, clearColor);
	m_context->ClearDepthStencilView(rt->dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	// Render frame 0
	LOG_TRACE("  Rendering frame 0 to thumbnail...");
	bool renderSuccess = RenderFrame(0);
	if (!renderSuccess) {
		LOG_WARN("  Thumbnail rendering returned false (may have issues)");
	}

	// Restore render state (comprehensive state restoration)
	LOG_TRACE("  Restoring previous GPU render state...");
	m_context->OMSetRenderTargets(1, &oldRTV, oldDSV);
	m_context->RSSetViewports(1, &oldViewport);
	m_context->RSSetState(oldRS);
	m_context->OMSetBlendState(oldBS, oldBlendFactor, oldSampleMask);
	m_context->OMSetDepthStencilState(oldDSS, oldStencilRef);

	// Release saved state objects
	if (oldRTV) oldRTV->Release();
	if (oldDSV) oldDSV->Release();
	if (oldRS) oldRS->Release();
	if (oldBS) oldBS->Release();
	if (oldDSS) oldDSS->Release();

	// Return SRV (caller takes ownership)
	ID3D11ShaderResourceView* srv = rt->srv;
	rt->srv = nullptr; // Prevent destructor from releasing it

	LOG_INFO("Thumbnail generated successfully: {}x{} (SRV={})", size, size, (void*)srv);
	return srv;
}

} // namespace S3D
