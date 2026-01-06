#include "S3DRendererDX7.h"

#include <algorithm>
#include <cstring>
#ifndef DIRECTDRAW_VERSION
#define DIRECTDRAW_VERSION 0x0700
#endif
#ifndef DIRECT3D_VERSION
#define DIRECT3D_VERSION 0x0700
#endif
#include <d3d.h>
#include <ddraw.h>
#include <d3dtypes.h>

#include "cGZPersistResourceKey.h"
#include "cIGZPersistDBRecord.h"
#include "cIGZPersistResourceKeyList.h"
#include "cIGZPersistResourceManager.h"
#include "cRZAutoRefCount.h"
#include "FSHReader.h"
#include "PersistResourceKeyFilterByTypeAndInstance.h"
#include "S3DEnumMappings.h"
#include "../gfx/DX7ImageLoader.h"
#include "../utils/Logger.h"

namespace {
	namespace RenderConstantsDX7 {
		constexpr float BILLBOARD_ROTATION_Y = -22.5f;
		constexpr float BILLBOARD_ROTATION_X = 45.0f;
		constexpr float BOUNDING_BOX_PADDING = 1.10f;
		constexpr float NEAR_PLANE = -40000.0f;
		constexpr float FAR_PLANE = 40000.0f;
	}

	constexpr DWORD kFVF = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;

	DWORD MapAlphaFunc(uint8_t glFunc) {
		switch (glFunc) {
		case S3D::EnumMappings::GL_NEVER: return D3DCMP_NEVER;
		case S3D::EnumMappings::GL_LESS: return D3DCMP_LESS;
		case S3D::EnumMappings::GL_EQUAL: return D3DCMP_EQUAL;
		case S3D::EnumMappings::GL_LEQUAL: return D3DCMP_LESSEQUAL;
		case S3D::EnumMappings::GL_GREATER: return D3DCMP_GREATER;
		case S3D::EnumMappings::GL_NOTEQUAL: return D3DCMP_NOTEQUAL;
		case S3D::EnumMappings::GL_GEQUAL: return D3DCMP_GREATEREQUAL;
		case S3D::EnumMappings::GL_ALWAYS: return D3DCMP_ALWAYS;
		default: return D3DCMP_GREATER;
		}
	}

	DWORD MapBlendFactor(uint8_t glBlend) {
		if (glBlend == S3D::EnumMappings::GL_ZERO) return D3DBLEND_ZERO;
		if (glBlend == S3D::EnumMappings::GL_ONE) return D3DBLEND_ONE;
		if (glBlend == S3D::EnumMappings::GL_SRC_COLOR) return D3DBLEND_SRCCOLOR;
		if (glBlend == S3D::EnumMappings::GL_ONE_MINUS_SRC_COLOR) return D3DBLEND_INVSRCCOLOR;
		if (glBlend == S3D::EnumMappings::GL_SRC_ALPHA) return D3DBLEND_SRCALPHA;
		if (glBlend == S3D::EnumMappings::GL_ONE_MINUS_SRC_ALPHA) return D3DBLEND_INVSRCALPHA;
		if (glBlend == S3D::EnumMappings::GL_DST_ALPHA) return D3DBLEND_DESTALPHA;
		if (glBlend == S3D::EnumMappings::GL_ONE_MINUS_DST_ALPHA) return D3DBLEND_INVDESTALPHA;
		if (glBlend == S3D::EnumMappings::GL_DST_COLOR) return D3DBLEND_DESTCOLOR;
		if (glBlend == S3D::EnumMappings::GL_ONE_MINUS_DST_COLOR) return D3DBLEND_INVDESTCOLOR;
		if (glBlend == S3D::EnumMappings::GL_SRC_ALPHA_SATURATE) return D3DBLEND_SRCALPHASAT;
		return D3DBLEND_ONE;
	}

	DWORD MapTextureWrap(uint8_t glWrap) {
		switch (glWrap) {
		case S3D::EnumMappings::GL_REPEAT: return D3DTADDRESS_WRAP;
		case S3D::EnumMappings::GL_CLAMP:
		case S3D::EnumMappings::GL_CLAMP_TO_EDGE: return D3DTADDRESS_CLAMP;
		case S3D::EnumMappings::GL_MIRRORED_REPEAT: return D3DTADDRESS_MIRROR;
		default: return D3DTADDRESS_WRAP;
		}
	}

	uint32_t PackColor(const DirectX::SimpleMath::Vector4& color) {
		const float r = std::clamp(color.x, 0.0f, 1.0f);
		const float g = std::clamp(color.y, 0.0f, 1.0f);
		const float b = std::clamp(color.z, 0.0f, 1.0f);
		const float a = std::clamp(color.w, 0.0f, 1.0f);
		const auto ri = static_cast<uint32_t>(r * 255.0f + 0.5f);
		const auto gi = static_cast<uint32_t>(g * 255.0f + 0.5f);
		const auto bi = static_cast<uint32_t>(b * 255.0f + 0.5f);
		const auto ai = static_cast<uint32_t>(a * 255.0f + 0.5f);
		return (ai << 24) | (ri << 16) | (gi << 8) | bi;
	}

	DWORD MapTextureMinFilter(uint8_t glFilter) {
		if (glFilter == S3D::EnumMappings::GL_NEAREST ||
			glFilter == S3D::EnumMappings::GL_NEAREST_MIPMAP_NEAREST ||
			glFilter == S3D::EnumMappings::GL_NEAREST_MIPMAP_LINEAR) {
			return D3DTFN_POINT;
		}
		return D3DTFN_LINEAR;
	}

	DWORD MapTextureMagFilter(uint8_t glFilter) {
		if (glFilter == S3D::EnumMappings::GL_NEAREST) {
			return D3DTFG_POINT;
		}
		return D3DTFG_LINEAR;
	}

	IDirectDrawSurface7* LoadFSHTextureSurface(
		cIGZPersistResourceManager* pRM,
		IDirectDraw7* ddraw,
		uint32_t groupID,
		uint32_t instanceID
	) {
		if (!pRM || !ddraw) {
			return nullptr;
		}

		constexpr uint32_t kFSHType = 0x7AB50E44;
		cGZPersistResourceKey key(kFSHType, groupID, instanceID);
		cIGZPersistDBRecord* pRecord = nullptr;
		if (!pRM->OpenDBRecord(key, &pRecord, false)) {
			cRZAutoRefCount<cIGZPersistResourceKeyList> pKeyList;
			pRM->GetAvailableResourceList(pKeyList.AsPPObj(),
			                              new PersistResourceKeyFilterByTypeAndInstance(kFSHType, instanceID));
			if (!pKeyList) {
				LOG_WARN("FSH texture not found for instance 0x{:08X}", instanceID);
				return nullptr;
			}

			bool found = false;
			for (uint32_t i = 0; i < pKeyList->Size(); ++i) {
				key = pKeyList->GetKey(i);
				if (pRM->OpenDBRecord(key, &pRecord, false)) {
					found = true;
					break;
				}
			}

			if (!found) {
				return nullptr;
			}
		}

		cRZAutoRefCount<cIGZPersistDBRecord> record(pRecord);
		uint32_t dataSize = record->GetSize();
		if (dataSize == 0) {
			return nullptr;
		}

		std::vector<uint8_t> fshData(dataSize);
		if (!record->GetFieldVoid(fshData.data(), dataSize)) {
			return nullptr;
		}

		FSH::File fshFile;
		if (!FSH::Reader::Parse(fshData.data(), dataSize, fshFile)) {
			return nullptr;
		}

		const FSH::Bitmap* mainBitmap = fshFile.GetMainBitmap();
		if (!mainBitmap) {
			return nullptr;
		}

		if (mainBitmap->IsCompressed()) {
			DWORD fourcc = 0;
			if (mainBitmap->code == FSH::CODE_DXT1) {
				fourcc = MAKEFOURCC('D', 'X', 'T', '1');
			}
			else if (mainBitmap->code == FSH::CODE_DXT3) {
				fourcc = MAKEFOURCC('D', 'X', 'T', '3');
			}
			else {
				return nullptr;
			}

			DDSURFACEDESC2 desc{};
			desc.dwSize = sizeof(desc);
			desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_LINEARSIZE;
			desc.dwWidth = mainBitmap->width;
			desc.dwHeight = mainBitmap->height;
			desc.dwLinearSize = static_cast<DWORD>(mainBitmap->GetExpectedDataSize());
			desc.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY;

			DDPIXELFORMAT pf{};
			pf.dwSize = sizeof(pf);
			pf.dwFlags = DDPF_FOURCC;
			pf.dwFourCC = fourcc;
			desc.ddpfPixelFormat = pf;

			IDirectDrawSurface7* surface = nullptr;
			if (FAILED(ddraw->CreateSurface(&desc, &surface, nullptr))) {
				desc.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_SYSTEMMEMORY;
				if (FAILED(ddraw->CreateSurface(&desc, &surface, nullptr))) {
					return nullptr;
				}
			}

			DDSURFACEDESC2 lockDesc{};
			lockDesc.dwSize = sizeof(lockDesc);
			if (FAILED(surface->Lock(nullptr, &lockDesc, 0, nullptr))) {
				surface->Release();
				return nullptr;
			}

			const uint8_t* src = mainBitmap->data.data();
			const uint32_t blockBytes = (mainBitmap->code == FSH::CODE_DXT1) ? 8u : 16u;
			const uint32_t blocksWide = (mainBitmap->width + 3) / 4;
			const uint32_t blocksHigh = (mainBitmap->height + 3) / 4;
			const uint32_t rowBytes = blocksWide * blockBytes;

			for (uint32_t y = 0; y < blocksHigh; ++y) {
				uint8_t* dst = static_cast<uint8_t*>(lockDesc.lpSurface) + y * lockDesc.lPitch;
				std::memcpy(dst, src + y * rowBytes, rowBytes);
			}

			surface->Unlock(nullptr);
			return surface;
		}

		std::vector<uint8_t> rgba;
		if (!FSH::Reader::ConvertToRGBA8(*mainBitmap, rgba)) {
			return nullptr;
		}

		IDirectDrawSurface7* surface = nullptr;
		if (!gfx::CreateSurfaceFromRGBA(
			rgba.data(),
			static_cast<int>(mainBitmap->width),
			static_cast<int>(mainBitmap->height),
			ddraw,
			&surface)) {
			return nullptr;
		}

		return surface;
	}
} // namespace

namespace S3D {
	RendererDX7::RendererDX7(IDirect3DDevice7* device, IDirectDraw7* ddraw)
		: m_device(device)
		  , m_ddraw(ddraw) {}

	RendererDX7::~RendererDX7() {
		ClearModel();
	}

	void RendererDX7::ClearModel() {
		for (auto& material : m_materials) {
			if (material.texture) {
				material.texture->Release();
				material.texture = nullptr;
			}
		}

		m_vertexBuffers.clear();
		m_indexBuffers.clear();
		m_primitiveBlocks.clear();
		m_materials.clear();
		m_frames.clear();
		m_meshes.clear();
		m_modelLoaded = false;
	}

	bool RendererDX7::LoadMaterials(const Model& model, cIGZPersistResourceManager* pRM, uint32_t groupID) {
		m_materials.clear();
		m_materials.reserve(model.materials.size());

		for (const auto& material : model.materials) {
			MaterialDX7 mat{};
			mat.alphaThreshold = material.alphaThreshold;
			mat.alphaTest = (material.flags & MAT_ALPHA_TEST) != 0;
			mat.alphaBlend = (material.flags & MAT_BLEND) != 0;
			mat.zEnable = (material.flags & MAT_DEPTH_TEST) != 0;
			mat.zWrite = (material.flags & MAT_DEPTH_WRITES) != 0;
			mat.alphaFunc = MapAlphaFunc(material.alphaFunc);
			mat.alphaRef = static_cast<DWORD>(std::clamp(mat.alphaThreshold, 0.0f, 1.0f) * 255.0f);
			mat.srcBlend = MapBlendFactor(material.srcBlend);
			mat.dstBlend = MapBlendFactor(material.dstBlend);
			mat.cullMode = (material.flags & MAT_BACKFACE_CULLING) ? D3DCULL_CCW : D3DCULL_NONE;
			mat.addressU = D3DTADDRESS_WRAP;
			mat.addressV = D3DTADDRESS_WRAP;
			mat.minFilter = D3DTFN_LINEAR;
			mat.magFilter = D3DTFG_LINEAR;

			if (!material.textures.empty()) {
				const auto& tex = material.textures[0];
				mat.addressU = MapTextureWrap(tex.wrapS);
				mat.addressV = MapTextureWrap(tex.wrapT);
				mat.minFilter = MapTextureMinFilter(tex.minFilter);
				mat.magFilter = MapTextureMagFilter(tex.magFilter);

				if (tex.textureID != 0) {
					mat.texture = LoadFSHTextureSurface(pRM, m_ddraw, groupID, tex.textureID);
				}
			}

			m_materials.push_back(mat);
		}

		return true;
	}

	bool RendererDX7::LoadModel(const Model& model, cIGZPersistResourceManager* pRM, uint32_t groupID) {
		ClearModel();

		if (!m_device || !m_ddraw) {
			return false;
		}

		if (!LoadMaterials(model, pRM, groupID)) {
			return false;
		}

		m_vertexBuffers.reserve(model.vertexBuffers.size());
		for (const auto& vb : model.vertexBuffers) {
			std::vector<VertexDX7> converted;
			converted.reserve(vb.vertices.size());
			for (const auto& v : vb.vertices) {
				VertexDX7 out{};
				out.x = v.position.x;
				out.y = v.position.y;
				out.z = v.position.z;
				out.color = PackColor(v.color);
				out.u = v.uv.x;
				out.v = v.uv.y;
				converted.push_back(out);
			}
			m_vertexBuffers.emplace_back(std::move(converted));
		}

		m_indexBuffers.reserve(model.indexBuffers.size());
		for (const auto& ib : model.indexBuffers) {
			m_indexBuffers.push_back(ib.indices);
		}

		m_primitiveBlocks = model.primitiveBlocks;
		m_frames = model.animation.animatedMeshes.empty()
			           ? std::vector<Frame>()
			           : model.animation.animatedMeshes[0].frames;
		m_meshes = model.animation.animatedMeshes;

		m_bbMin = model.bbMin;
		m_bbMax = model.bbMax;
		m_modelLoaded = true;
		return true;
	}

	RendererDX7::RenderTargetDX7* RendererDX7::CreateRenderTarget(uint32_t width, uint32_t height) {
		if (!m_ddraw) return nullptr;

		DDSURFACEDESC2 desc{};
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
		desc.dwWidth = width;
		desc.dwHeight = height;
		desc.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_3DDEVICE | DDSCAPS_VIDEOMEMORY;

		DDPIXELFORMAT pf{};
		pf.dwSize = sizeof(pf);
		pf.dwFlags = DDPF_ALPHAPIXELS | DDPF_RGB;
		pf.dwRGBBitCount = 32;
		pf.dwRGBAlphaBitMask = 0xFF000000;
		pf.dwRBitMask = 0x00FF0000;
		pf.dwGBitMask = 0x0000FF00;
		pf.dwBBitMask = 0x000000FF;
		desc.ddpfPixelFormat = pf;

		IDirectDrawSurface7* color = nullptr;
		if (FAILED(m_ddraw->CreateSurface(&desc, &color, nullptr))) {
			return nullptr;
		}

		auto* rt = new RenderTargetDX7();
		rt->color = color;
		rt->width = width;
		rt->height = height;

		DDSURFACEDESC2 zDesc{};
		zDesc.dwSize = sizeof(zDesc);
		zDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
		zDesc.dwWidth = width;
		zDesc.dwHeight = height;
		zDesc.ddsCaps.dwCaps = DDSCAPS_ZBUFFER | DDSCAPS_VIDEOMEMORY;

		DDPIXELFORMAT zpf{};
		zpf.dwSize = sizeof(zpf);
		zpf.dwFlags = DDPF_ZBUFFER;
		zpf.dwZBufferBitDepth = 16;
		zDesc.ddpfPixelFormat = zpf;

		IDirectDrawSurface7* zbuffer = nullptr;
		if (SUCCEEDED(m_ddraw->CreateSurface(&zDesc, &zbuffer, nullptr))) {
			if (FAILED(color->AddAttachedSurface(zbuffer))) {
				zbuffer->Release();
				zbuffer = nullptr;
			}
		}

		rt->zbuffer = zbuffer;
		return rt;
	}

	void RendererDX7::CalculateViewProj(DirectX::SimpleMath::Matrix& outView,
	                                    DirectX::SimpleMath::Matrix& outProj) const {
		using namespace DirectX;
		using namespace DirectX::SimpleMath;

		const float ry_deg = RenderConstantsDX7::BILLBOARD_ROTATION_Y;
		const float rx_deg = RenderConstantsDX7::BILLBOARD_ROTATION_X;

		Matrix rotY_pos = Matrix::CreateRotationY(XMConvertToRadians(22.5f));
		Matrix rotX_neg = Matrix::CreateRotationX(XMConvertToRadians(-45.0f));

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
			minX = (std::min)(minX, v.x);
			maxX = (std::max)(maxX, v.x);
			minY = (std::min)(minY, v.y);
			maxY = (std::max)(maxY, v.y);
			maxZ = (std::max)(maxZ, v.z);
		}

		const float padding = RenderConstantsDX7::BOUNDING_BOX_PADDING;
		float width = (maxX - minX);
		float height = (maxY - minY);
		float diff = (std::max)(width, height) * padding;
		if (diff < 1e-4f) {
			diff = 1.0f;
		}

		float posx = (minX + maxX) * 0.5f;
		float posy = (minY + maxY) * 0.5f;
		float posz = maxZ;

		Matrix view = Matrix::Identity;
		view *= Matrix::CreateRotationY(XMConvertToRadians(ry_deg));
		view *= Matrix::CreateRotationX(XMConvertToRadians(rx_deg));
		view *= Matrix::CreateTranslation(-posx, -posy, -posz);

		Matrix proj = Matrix(XMMatrixOrthographicLH(diff, diff,
		                                            RenderConstantsDX7::NEAR_PLANE, RenderConstantsDX7::FAR_PLANE));

		outView = view;
		outProj = proj;
	}

	bool RendererDX7::ApplyMaterial(const MaterialDX7& material) {
		if (!m_device) return false;

		m_device->SetRenderState(D3DRENDERSTATE_ALPHATESTENABLE, material.alphaTest ? TRUE : FALSE);
		m_device->SetRenderState(D3DRENDERSTATE_ALPHAFUNC, material.alphaFunc);
		m_device->SetRenderState(D3DRENDERSTATE_ALPHAREF, material.alphaRef);
		m_device->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, material.alphaBlend ? TRUE : FALSE);
		m_device->SetRenderState(D3DRENDERSTATE_SRCBLEND, material.srcBlend);
		m_device->SetRenderState(D3DRENDERSTATE_DESTBLEND, material.dstBlend);
		m_device->SetRenderState(D3DRENDERSTATE_CULLMODE, material.cullMode);
		m_device->SetRenderState(D3DRENDERSTATE_ZENABLE, material.zEnable ? D3DZB_TRUE : D3DZB_FALSE);
		m_device->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, material.zWrite ? TRUE : FALSE);

		m_device->SetTexture(0, material.texture);
		if (material.texture) {
			m_device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
			m_device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
			m_device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
			m_device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
			m_device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
			m_device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
		}
		else {
			m_device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
			m_device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
			m_device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
			m_device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
		}
		m_device->SetTextureStageState(0, D3DTSS_ADDRESSU, material.addressU);
		m_device->SetTextureStageState(0, D3DTSS_ADDRESSV, material.addressV);
		m_device->SetTextureStageState(0, D3DTSS_MINFILTER, material.minFilter);
		m_device->SetTextureStageState(0, D3DTSS_MAGFILTER, material.magFilter);

		return true;
	}

	bool RendererDX7::RenderFrame(int frameIdx) {
		if (!m_modelLoaded || m_meshes.empty()) {
			return false;
		}

		DirectX::SimpleMath::Matrix view, proj;
		CalculateViewProj(view, proj);

		D3DMATRIX d3dView = *reinterpret_cast<D3DMATRIX*>(&view);
		D3DMATRIX d3dProj = *reinterpret_cast<D3DMATRIX*>(&proj);
		m_device->SetTransform(D3DTRANSFORMSTATE_VIEW, &d3dView);
		m_device->SetTransform(D3DTRANSFORMSTATE_PROJECTION, &d3dProj);
		D3DMATRIX identity{};
		identity._11 = 1.0f;
		identity._22 = 1.0f;
		identity._33 = 1.0f;
		identity._44 = 1.0f;
		m_device->SetTransform(D3DTRANSFORMSTATE_WORLD, &identity);

		m_device->SetRenderState(D3DRENDERSTATE_LIGHTING, FALSE);
		m_device->SetRenderState(D3DRENDERSTATE_COLORVERTEX, TRUE);

		for (size_t meshIdx = 0; meshIdx < m_meshes.size(); ++meshIdx) {
			const auto& mesh = m_meshes[meshIdx];
			if (frameIdx >= static_cast<int>(mesh.frames.size())) {
				continue;
			}

			const auto& frame = mesh.frames[frameIdx];
			if (frame.vertBlock >= m_vertexBuffers.size() ||
				frame.indexBlock >= m_indexBuffers.size() ||
				frame.matsBlock >= m_materials.size()) {
				continue;
			}

			const auto& vb = m_vertexBuffers[frame.vertBlock];
			const auto& ib = m_indexBuffers[frame.indexBlock];
			const auto& material = m_materials[frame.matsBlock];

			ApplyMaterial(material);

			if (frame.primBlock < m_primitiveBlocks.size() && !m_primitiveBlocks[frame.primBlock].empty()) {
				const auto& primBlock = m_primitiveBlocks[frame.primBlock];
				for (const auto& prim : primBlock) {
					D3DPRIMITIVETYPE topology = D3DPT_TRIANGLELIST;
					switch (prim.type) {
					case 0: topology = D3DPT_TRIANGLELIST;
						break;
					case 1: topology = D3DPT_TRIANGLESTRIP;
						break;
					case 2: topology = D3DPT_TRIANGLEFAN;
						break;
					default: continue;
					}

					if (prim.first + prim.length > ib.size()) {
						continue;
					}

					m_device->DrawIndexedPrimitive(
						topology,
						kFVF,
						const_cast<VertexDX7*>(vb.data()),
						static_cast<DWORD>(vb.size()),
						const_cast<WORD*>(&ib[prim.first]),
						prim.length,
						0);
				}
			}
			else {
				if (!ib.empty()) {
					m_device->DrawIndexedPrimitive(
						D3DPT_TRIANGLELIST,
						kFVF,
						const_cast<VertexDX7*>(vb.data()),
						static_cast<DWORD>(vb.size()),
						const_cast<WORD*>(ib.data()),
						static_cast<DWORD>(ib.size()),
						0);
				}
			}
		}

		return true;
	}

	IDirectDrawSurface7* RendererDX7::GenerateThumbnail(int size) {
		if (!m_device || !m_ddraw || !m_modelLoaded) {
			return nullptr;
		}

        DWORD stateBlock = 0;
        if (SUCCEEDED(m_device->CreateStateBlock(D3DSBT_ALL, &stateBlock)) && stateBlock != 0) {
                m_device->CaptureStateBlock(stateBlock);
        }

		std::unique_ptr<RenderTargetDX7> rt(
			CreateRenderTarget(static_cast<uint32_t>(size), static_cast<uint32_t>(size)));
		if (!rt || !rt->color) {
                if (stateBlock != 0) {
                        m_device->ApplyStateBlock(stateBlock);
                        m_device->DeleteStateBlock(stateBlock);
                }
                return nullptr;
        }

		IDirectDrawSurface7* oldRT = nullptr;
		if (FAILED(m_device->GetRenderTarget(&oldRT))) {
                if (stateBlock != 0) {
                        m_device->ApplyStateBlock(stateBlock);
                        m_device->DeleteStateBlock(stateBlock);
                }
                return nullptr;
        }

		if (FAILED(m_device->SetRenderTarget(rt->color, 0))) {
                oldRT->Release();
                if (stateBlock != 0) {
                        m_device->ApplyStateBlock(stateBlock);
                        m_device->DeleteStateBlock(stateBlock);
                }
                return nullptr;
        }

		D3DVIEWPORT7 oldViewport{};
		m_device->GetViewport(&oldViewport);

		D3DVIEWPORT7 viewport{};
		viewport.dwX = 0;
		viewport.dwY = 0;
		viewport.dwWidth = size;
		viewport.dwHeight = size;
		viewport.dvMinZ = 0.0f;
		viewport.dvMaxZ = 1.0f;
		m_device->SetViewport(&viewport);

		m_device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0x00000000, 1.0f, 0);

		bool beganScene = false;
		HRESULT beginResult = m_device->BeginScene();
		if (SUCCEEDED(beginResult)) {
			beganScene = true;
		}

		RenderFrame(0);

		if (beganScene) {
			m_device->EndScene();
		}

		m_device->SetViewport(&oldViewport);
		m_device->SetRenderTarget(oldRT, 0);
		oldRT->Release();

        if (stateBlock != 0) {
                m_device->ApplyStateBlock(stateBlock);
                m_device->DeleteStateBlock(stateBlock);
        }

		IDirectDrawSurface7* result = rt->color;
		result->AddRef();
		return result;
	}
} // namespace S3D
