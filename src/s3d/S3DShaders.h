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
    // Fix UV V-coordinate flip (SC4 uses top-origin, D3D11 uses bottom-origin)
    output.uv = float2(input.uv.x, 1.0 - input.uv.y);
    return output;
}
)";

// Pixel shader source
constexpr const char* PIXEL_SHADER = R"(
cbuffer MaterialConstants : register(b0)
{
    float alphaThreshold;
    float3 padding;
};

Texture2D txDiffuse : register(t0);
SamplerState samLinear : register(s0);

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_TARGET
{
    float4 texColor = txDiffuse.Sample(samLinear, input.uv);
    float4 finalColor = texColor * input.color;

    // Alpha test (discard pixels with alpha <= threshold, matching OpenGL's glAlphaFunc(GL_GREATER, threshold))
    if (finalColor.a <= alphaThreshold) {
        discard;
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
