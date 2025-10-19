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
    output.position = mul(float4(input.position, 1.0), viewProj);
    output.color = input.color;
    output.uv = input.uv;
    return output;
}
)";

// Pixel shader source
constexpr const char* PIXEL_SHADER = R"(
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
    return texColor * input.color;
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
