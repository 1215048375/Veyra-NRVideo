// Deterministic Phase 1 test pattern (Playbook section 16 Phase 1).
// Pure function of pixel coordinates and frameId: quadrant layout with an
// animated horizontal gradient, an 8-pixel checker, a hard diagonal edge and
// vertical RGB bars. For a fixed frameId the output is fully deterministic.

cbuffer FrameParams : register(b0)
{
    uint frameId;
    uint patternWidth;
    uint patternHeight;
    uint padding0;
};

RWTexture2D<unorm float4> outputTexture : register(u0);

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= patternWidth || dispatchThreadId.y >= patternHeight) {
        return;
    }

    const uint x = dispatchThreadId.x;
    const uint y = dispatchThreadId.y;
    const float2 uv = float2(x / float(patternWidth), y / float(patternHeight));
    // Gradient phase shifts 1/8 of the width per frame; wraps every 8 frames.
    const float shift = frac(frameId * 0.125);

    float4 color = float4(0.0, 0.0, 0.0, 1.0);

    const bool rightHalf = x >= (patternWidth / 2);
    const bool bottomHalf = y >= (patternHeight / 2);

    if (!rightHalf && !bottomHalf) {
        // Top-left: animated gradient.
        color.rgb = frac(uv.x + shift);
    }
    else if (rightHalf && !bottomHalf) {
        // Top-right: 8-pixel checker.
        const float checker = (((x / 8) + (y / 8)) & 1) != 0 ? 0.85 : 0.15;
        color.rgb = checker;
    }
    else if (!rightHalf && bottomHalf) {
        // Bottom-left: hard diagonal edge.
        const float edge = (uv.x + uv.y) >= 1.0 ? 0.9 : 0.05;
        color.rgb = edge;
    }
    else {
        // Bottom-right: vertical RGB bars.
        const uint third = patternWidth / 3;
        const uint column = x % third;
        if (column < (third / 3)) {
            color.r = 1.0;
        }
        else if (column < (2 * (third / 3))) {
            color.g = 1.0;
        }
        else {
            color.b = 1.0;
        }
    }

    outputTexture[dispatchThreadId.xy] = color;
}
