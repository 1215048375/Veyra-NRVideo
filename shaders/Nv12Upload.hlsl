// Nv12Upload: copies an NV12 byte buffer (luma rows, then chroma rows,
// 256-aligned row pitches) into R8_UNORM luma and R8G8_UNORM chroma textures
// via compute. Used instead of CopyTextureRegion because the injected D3D12
// layer on this system poisons command-list Close after any copy call once
// descriptor views exist (diagnosed 2026-09-04).

ByteAddressBuffer nv12Bytes : register(t0);

RWTexture2D<float> lumaOut : register(u0);
RWTexture2D<float2> chromaOut : register(u1);

cbuffer Nv12UploadConstants : register(b0)
{
    uint sourceWidth;
    uint sourceHeight;
    uint lumaRowPitch;   // bytes
    uint chromaRowPitch; // bytes
    uint lumaByteOffset;
    uint chromaByteOffset;
    float _pad0;
    float _pad1;
};

[numthreads(16, 16, 1)]
void main(uint3 globalId : SV_DispatchThreadID)
{
    // Luma plane: one thread per luma pixel.
    if (globalId.x < sourceWidth && globalId.y < sourceHeight) {
        const uint offset = lumaByteOffset + globalId.y * lumaRowPitch + globalId.x;
        lumaOut[globalId.xy] = asfloat(0x00000000 | nv12Bytes.Load(offset));
    }
    // Chroma plane: threads in the top-left quadrant cover chroma pixels.
    const uint chromaW = sourceWidth / 2;
    const uint chromaH = sourceHeight / 2;
    if (globalId.x < chromaW && globalId.y < chromaH) {
        const uint offset = chromaByteOffset + globalId.y * chromaRowPitch + globalId.x * 2;
        const uint two = nv12Bytes.Load(offset);
        // R8_UNORM byte -> normalized float: divide by 255.
        const float u = float(two & 0xFF) / 255.0;
        const float v = float((two >> 8) & 0xFF) / 255.0;
        chromaOut[uint2(globalId.x, globalId.y)] = float2(u, v);
    }
}
