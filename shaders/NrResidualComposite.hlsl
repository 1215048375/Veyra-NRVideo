Texture2D<float4> baseTex:register(t0);
Texture2D<float4> nrInput:register(t1);
Texture2D<float4> nrFinal:register(t2);
RWTexture2D<float4> outputTex:register(u0);
cbuffer Params:register(b0){float total;float darken;float brighten;float color;float luminance;float3 unused;}
// Bilateral resampling of the change uses the preserved high-resolution base
// as the edge guide. It never interpolates the high-resolution base itself.
[numthreads(16,16,1)] void main(uint3 id:SV_DispatchThreadID){
    uint w,h,nw,nh;baseTex.GetDimensions(w,h);nrInput.GetDimensions(nw,nh);
    if(id.x>=w||id.y>=h)return;
    float4 base=baseTex[id.xy];
    if(total==0){outputTex[id.xy]=base;return;}
    float2 pos=(float2(id.xy)+0.5)*float2(nw,nh)/float2(w,h)-0.5;
    int2 origin=(int2)floor(pos);float2 f=frac(pos);float3 delta=0;float weights=0;
    for(int y=0;y<2;++y)for(int x=0;x<2;++x){
        int2 q=clamp(origin+int2(x,y),int2(0,0),int2(nw-1,nh-1));
        float3 low=nrInput[q].rgb;
        float weight=(x?f.x:1-f.x)*(y?f.y:1-f.y);
        if(w!=nw||h!=nh)weight/=1+16*dot(abs(base.rgb-low),float3(0.2126,0.7152,0.0722));
        delta+=(nrFinal[q].rgb-low)*weight;weights+=weight;
    }
    delta/=max(weights,1e-6);
    if(darken!=1||brighten!=1)delta=min(delta,0)*darken+max(delta,0)*brighten;
    if(color!=1||luminance!=1){float dy=dot(delta,float3(0.2126,0.7152,0.0722));delta=dy*luminance+(delta-dy)*color;}
    outputTex[id.xy]=float4(max(0,base.rgb+total*delta),base.a);
}
