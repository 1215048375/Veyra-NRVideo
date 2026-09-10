cbuffer RgbParams : register(b0) { uint width; uint height; uint transfer; uint limited; };
Texture2D<float4> sourceRgb : register(t0);
RWTexture2D<float4> linearRgb : register(u0);
float decode(float v) {
    if (transfer == 2) return v < 0.081 ? v / 4.5 : pow((v + 0.099) / 1.099, 1.0 / 0.45);
    if (transfer == 0) return v;
    return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}
[numthreads(16,16,1)]
void main(uint3 p : SV_DispatchThreadID) {
    if (p.x >= width || p.y >= height) return;
    float4 c=sourceRgb[p.xy];
    if(limited!=0)c.rgb=saturate((c.rgb-16.0/255.0)*(255.0/219.0));
    // Present onto the player's opaque black canvas; never discard RGB chroma.
    linearRgb[p.xy]=float4(float3(decode(c.r),decode(c.g),decode(c.b))*c.a,1);
}
