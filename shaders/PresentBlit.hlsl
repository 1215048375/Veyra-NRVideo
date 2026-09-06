// PresentBlit: pixel-shader blit of the working frame onto a flip-model
// swapchain back buffer. Flip back buffers may only transition between
// PRESENT and RENDER_TARGET (not UAV), so the present path is a fullscreen
// triangle sample instead of a compute write.

Texture2D<float4> sourceTex : register(t0);

cbuffer PresentBlitConstants : register(b0)
{
    float4 srcDims; // x=width y=height zw=pad
    float4 dstDims; // x=width y=height zw=pad
};

SamplerState linearClamp : register(s0);

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOut vsMain(uint vertexId : SV_VertexID)
{
    VSOut o;
    const float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    o.uv = uv;
    return o;
}

float4 psMain(VSOut input) : SV_Target
{
    (void)srcDims;
    (void)dstDims;
    return sourceTex.SampleLevel(linearClamp, input.uv, 0);
}
