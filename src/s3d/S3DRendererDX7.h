#pragma once
#include <d3d.h>
#include <ddraw.h>
#include <SimpleMath.h>
#include <vector>

#include "S3DStructures.h"

class cIGZPersistResourceManager;

namespace S3D {

class RendererDX7 {
public:
    RendererDX7(IDirect3DDevice7* device, IDirectDraw7* ddraw);
    ~RendererDX7();

    bool LoadModel(const Model& model, cIGZPersistResourceManager* pRM, uint32_t groupID);
    void ClearModel();

    // Returns a render target surface that can be used with ImGui::Image.
    // Caller owns the returned surface and must Release() it.
    IDirectDrawSurface7* GenerateThumbnail(int size = 128);

    bool HasModel() const { return m_modelLoaded; }

private:
    struct VertexDX7 {
        float x;
        float y;
        float z;
        uint32_t color;
        float u;
        float v;
    };

    struct MaterialDX7 {
        IDirectDrawSurface7* texture = nullptr;
        DWORD alphaFunc = 0;
        DWORD alphaRef = 0;
        DWORD srcBlend = 0;
        DWORD dstBlend = 0;
        DWORD cullMode = 0;
        bool alphaTest = false;
        bool alphaBlend = false;
        bool zEnable = true;
        bool zWrite = true;
        DWORD addressU = 0;
        DWORD addressV = 0;
        DWORD minFilter = 0;
        DWORD magFilter = 0;
        float alphaThreshold = 0.5f;
    };

    struct RenderTargetDX7 {
        IDirectDrawSurface7* color = nullptr;
        IDirectDrawSurface7* zbuffer = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;

        ~RenderTargetDX7() {
            if (zbuffer) zbuffer->Release();
            if (color) color->Release();
        }
    };

    IDirect3DDevice7* m_device;
    IDirectDraw7* m_ddraw;

    std::vector<std::vector<VertexDX7>> m_vertexBuffers;
    std::vector<std::vector<uint16_t>> m_indexBuffers;
    std::vector<PrimitiveBlock> m_primitiveBlocks;
    std::vector<MaterialDX7> m_materials;
    std::vector<Frame> m_frames;
    std::vector<AnimatedMesh> m_meshes;

    DirectX::SimpleMath::Vector3 m_bbMin;
    DirectX::SimpleMath::Vector3 m_bbMax;
    bool m_modelLoaded = false;

    bool ApplyMaterial(const MaterialDX7& material);
    bool RenderFrame(int frameIdx = 0);
    RenderTargetDX7* CreateRenderTarget(uint32_t width, uint32_t height);
    void CalculateViewProj(DirectX::SimpleMath::Matrix& outView, DirectX::SimpleMath::Matrix& outProj) const;
    bool LoadMaterials(const Model& model, cIGZPersistResourceManager* pRM, uint32_t groupID);
};

} // namespace S3D
