// Distribution note (2026-09-09): project-owned additions and modifications
// are licensed under Apache-2.0. Upstream portions retain their original
// licenses and attribution; see NOTICE, PROVENANCE.md and licenses/.
// OpenCV DIS/reference-derived portions retain Intel (2000-2008), Willow
// Garage (2009) and other upstream copyrights and BSD/Apache terms.
// See licenses/OPENCV_DIS_BSD_HEADER.txt and OPENCV_APACHE_LICENSE.txt.

// Preserve original 8-bit Y bytes; do not range-expand limited Y or convert RGB.
Texture2D<float> Source : register(t0);
RWTexture2D<float> Destination : register(u0);
cbuffer Params : register(b0) { uint width; uint height; uint cropX; uint cropY; };
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) return;
    Destination[id.xy] = floor(Source.Load(int3(id.xy + uint2(cropX,cropY),0))*255.0+0.5);
}
