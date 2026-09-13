// Distribution note (2026-09-09): project-owned additions and modifications
// are licensed under Apache-2.0. Upstream portions retain their original
// licenses and attribution; see NOTICE, PROVENANCE.md and licenses/.
// OpenCV DIS/reference-derived portions retain Intel (2000-2008), Willow
// Garage (2009) and other upstream copyrights and BSD/Apache terms.
// See licenses/OPENCV_DIS_BSD_HEADER.txt and OPENCV_APACHE_LICENSE.txt.

// Compatibility ingress: D3D11 already produced exact FFmpeg 7.1 RGB bytes.
// Match dis_native_gray's OpenCV integer RGB2GRAY, not floating video luma.
Texture2D<float4> Color : register(t0);
RWTexture2D<float> Gray : register(u0);
cbuffer Params : register(b0) {
    uint width; uint height; uint cropX; uint cropY;
};
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if(id.x>=width || id.y>=height) return;
    uint3 rgb=(uint3)(Color.Load(int3(id.xy+uint2(cropX,cropY),0)).rgb*255.0+0.5);
    Gray[id.xy]=(float)((9798u*rgb.r+19235u*rgb.g+3735u*rgb.b+16384u)>>15);
}
