#pragma once

// Embedded HLSL shaders for S3D rendering
// These are compiled at runtime using D3DCompile

namespace S3D {
namespace Shaders {

// Vertex shader source
constexpr const char* VERTEX_SHADER = R"(
cbuffer Constants : register(b0)
{
    matrix viewProj;
};

struct VS_INPUT
{
    float3 position : POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
    float2 uv2 : TEXCOORD1;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
};

PS_INPUT main(VS_INPUT input)
{
    PS_INPUT output;
    // Use column-vector multiplication (standard for DirectX)
    output.position = mul(viewProj, float4(input.position, 1.0));
    output.color = input.color;
    // Pass UVs through as-is
    output.uv = input.uv;
    return output;
}
)";

// Pixel shader source with debug visualization support
constexpr const char* PIXEL_SHADER = R"(
cbuffer MaterialConstants : register(b0)
{
    float alphaThreshold;
    uint alphaFunc;      // 0=NEVER, 1=LESS, 2=EQUAL, 3=LEQUAL, 4=GREATER, 5=NOTEQUAL, 6=GEQUAL, 7=ALWAYS
    uint debugMode;      // 0=Normal, 1=Wireframe, 2=UVs, 3=VertexColor, 4=MaterialID, 5=Normals, 6=TextureOnly, 7=AlphaTest
    uint materialIndex;  // Material index for MaterialID mode
};

Texture2D txDiffuse : register(t0);
SamplerState samLinear : register(s0);

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
};

// Alpha test function
bool AlphaTest(float alpha, float threshold, uint func)
{
    if (func == 0) return false;                // NEVER
    if (func == 1) return alpha < threshold;    // LESS
    if (func == 2) return alpha == threshold;   // EQUAL
    if (func == 3) return alpha <= threshold;   // LEQUAL
    if (func == 4) return alpha > threshold;    // GREATER (default)
    if (func == 5) return alpha != threshold;   // NOTEQUAL
    if (func == 6) return alpha >= threshold;   // GEQUAL
    return true;                                 // ALWAYS
}

// Generate a unique color from a material index
float3 MaterialIDToColor(uint id)
{
    // Simple hash to get varied colors for different material IDs
    float r = frac(sin(float(id) * 12.9898) * 43758.5453);
    float g = frac(sin(float(id) * 78.233) * 43758.5453);
    float b = frac(sin(float(id) * 45.543) * 43758.5453);
    return float3(r, g, b);
}

float4 main(PS_INPUT input) : SV_TARGET
{
    float4 texColor = txDiffuse.Sample(samLinear, input.uv);
    float4 finalColor = texColor * input.color;

    // Debug visualization modes
    if (debugMode == 1) {
        // Wireframe mode - normal rendering (wireframe overlay done via rasterizer state)
        // Just render normally
    }
    else if (debugMode == 2) {
        // UVs mode - visualize UV coordinates as colors (R=U, G=V, B=0)
        finalColor = float4(input.uv.x, input.uv.y, 0.0, 1.0);
    }
    else if (debugMode == 3) {
        // VertexColor mode - show only vertex colors (no texture)
        finalColor = input.color;
    }
    else if (debugMode == 4) {
        // MaterialID mode - show unique color per material
        finalColor = float4(MaterialIDToColor(materialIndex), 1.0);
    }
    else if (debugMode == 5) {
        // Normals mode - we don't have normals in vertex data, show magenta as "not available"
        finalColor = float4(1.0, 0.0, 1.0, 1.0);
    }
    else if (debugMode == 6) {
        // TextureOnly mode - show texture without vertex color modulation
        finalColor = texColor;
    }
    else if (debugMode == 7) {
        // AlphaTest visualization - green if kept, red if would be discarded
        bool passes = AlphaTest(finalColor.a, alphaThreshold, alphaFunc);
        finalColor = passes ? float4(0.0, 1.0, 0.0, 1.0) : float4(1.0, 0.0, 0.0, 1.0);
    }

    // Alpha test (skip in debug modes except AlphaTest mode)
    if (debugMode == 0 || debugMode == 1 || debugMode == 6) {
        if (!AlphaTest(finalColor.a, alphaThreshold, alphaFunc)) {
            discard;
        }
    }

    return finalColor;
}
)";

// Pixel shader without texture (fallback)
constexpr const char* PIXEL_SHADER_NO_TEXTURE = R"(
struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_TARGET
{
    return input.color;
}
)";

} // namespace Shaders
} // namespace S3D
