// Linear BT.709 working values use scRGB units: 1 = 80 cd/m2.
// Keep out-of-709-gamut negative components until the final BT.2020 boundary.
float3 HdrTo2020(float3 c) {
    return mul(float3x3(.627404,.329283,.043313,.069097,.919540,.011362,.016391,.088013,.895595),c);
}
float3 HdrTo709(float3 c) {
    return mul(float3x3(1.660491,-.587641,-.072850,-.124550,1.132900,-.008349,-.018151,-.100579,1.118730),c);
}
float3 HdrEncodePq(float3 linear709) {
    float3 p=pow(saturate(HdrTo2020(linear709)*(.008)),2610.0/16384.0);
    return pow((3424.0/4096.0+(2413.0/128.0)*p)/(1+(2392.0/128.0)*p),2523.0/32.0);
}
float3 HdrDecodePq(float3 pq) {
    float3 p=pow(saturate(pq),32.0/2523.0);
    return HdrTo709(125.0*pow(max(p-3424.0/4096.0,0)/max(2413.0/128.0-2392.0/128.0*p,1e-6),16384.0/2610.0));
}
float3 HdrProxy(float3 hdr) {
    // Fixed 203-nit reference: no per-frame exposure pump. Only the proxy is
    // gamut-limited; the original HDR base is never reconstructed from it.
    float3 c=max(hdr*(80.0/203.0),0);
    return select(c<=.75,c,.75+.25*(1-exp(-5.778*max(c-.75,0))));
}
float3 HdrRestore(float3 base,float3 proxy,float3 enhanced) {
    float3 delta=enhanced-proxy;
    float y=max(dot(base,float3(.212639,.715169,.072192)),0);
    float peak=max(max(proxy.r,proxy.g),proxy.b);
    // Preserve compressed highlights and near-black; never divide by proxy
    // luminance. Zero delta is an exact identity including wide-gamut colors.
    float gate=smoothstep(0,.02,y)*(1-smoothstep(.75,.995,peak));
    return base+clamp(delta,-.5,.5)*(203.0/80.0)*gate;
}
