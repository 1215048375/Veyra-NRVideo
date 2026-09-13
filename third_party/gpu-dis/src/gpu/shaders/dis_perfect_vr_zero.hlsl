// Distribution note (2026-09-09): project-owned additions and modifications
// are licensed under Apache-2.0. Upstream portions retain their original
// licenses and attribution; see NOTICE, PROVENANCE.md and licenses/.
// OpenCV DIS/reference-derived portions retain Intel (2000-2008), Willow
// Garage (2009) and other upstream copyrights and BSD/Apache terms.
// See licenses/OPENCV_DIS_BSD_HEADER.txt and OPENCV_APACHE_LICENSE.txt.

RWTexture2D<float2> DFlow : register(u0);
cbuffer Params : register(b0) { uint width; uint height; };
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) { if (id.x < width && id.y < height) DFlow[id.xy] = 0.0.xx; }
