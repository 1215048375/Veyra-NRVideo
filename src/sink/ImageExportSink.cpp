#include "veyra/sink/ImageExportSink.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/Log.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <cstring>
#include <filesystem>
namespace veyra::sink {
using Microsoft::WRL::ComPtr;
bool readRgba8(gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& ring,ID3D12Resource* tex,RgbaImage& img) {
    if(!tex) return false;
    auto d=tex->GetDesc();
    if(d.Format!=DXGI_FORMAT_R8G8B8A8_UNORM) return false;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{}; UINT rows=0; UINT64 rowBytes=0,total=0;
    ctx.device()->GetCopyableFootprints(&d,0,1,0,&fp,&rows,&rowBytes,&total);
    D3D12_HEAP_PROPERTIES hp{}; hp.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bd{}; bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width=total;
    bd.Height=1; bd.DepthOrArraySize=1; bd.MipLevels=1; bd.SampleDesc.Count=1; bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> rb;
    if(FAILED(ctx.device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rb)))) return false;
    Status st=Status::Ok; const auto slot=ring.slotCount()-1; auto* list=ring.acquire(slot,st); if(!list)return false;
    D3D12_RESOURCE_BARRIER b{}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={tex,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE};
    list->ResourceBarrier(1,&b);
    D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=tex;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource=rb.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;
    list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);
    if(!ring.submitAndSignal(slot)||!ring.waitIdle())return false;
    uint8_t* data=nullptr; D3D12_RANGE range{0,static_cast<SIZE_T>(total)};
    if(FAILED(rb->Map(0,&range,reinterpret_cast<void**>(&data))))return false;
    img.width=static_cast<uint32_t>(d.Width);img.height=d.Height;img.pixels.resize(size_t(img.width)*img.height*4);
    for(UINT y=0;y<img.height;++y)std::memcpy(img.pixels.data()+size_t(y)*img.width*4,data+fp.Offset+size_t(y)*fp.Footprint.RowPitch,size_t(img.width)*4);
    D3D12_RANGE empty{0,0};rb->Unmap(0,&empty);return true;
}
bool loadImage(const std::wstring& path,RgbaImage& img) {
    ComPtr<IWICImagingFactory> fac;ComPtr<IWICBitmapDecoder> dec;ComPtr<IWICBitmapFrameDecode> frame;ComPtr<IWICFormatConverter> conv;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&fac)))||
       FAILED(fac->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&dec))||
       FAILED(dec->GetFrame(0,&frame))||FAILED(fac->CreateFormatConverter(&conv))||
       FAILED(conv->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))||
       FAILED(conv->GetSize(&img.width,&img.height))||img.width==0||img.height==0||img.width>8192||img.height>8192) return false;
    ComPtr<IWICMetadataQueryReader> metadata;
    UINT orientation=1;
    if(SUCCEEDED(frame->GetMetadataQueryReader(&metadata))){PROPVARIANT value{};
        if(SUCCEEDED(metadata->GetMetadataByName(L"/app1/ifd/{ushort=274}",&value))&&value.vt==VT_UI2)orientation=value.uiVal;
        PropVariantClear(&value);
    }
    const WICBitmapTransformOptions transforms[9]={WICBitmapTransformRotate0,WICBitmapTransformRotate0,WICBitmapTransformFlipHorizontal,WICBitmapTransformRotate180,WICBitmapTransformFlipVertical,
        static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90|WICBitmapTransformFlipHorizontal),WICBitmapTransformRotate90,
        static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270|WICBitmapTransformFlipHorizontal),WICBitmapTransformRotate270};
    ComPtr<IWICBitmapFlipRotator> oriented;
    if(FAILED(fac->CreateBitmapFlipRotator(&oriented))||FAILED(oriented->Initialize(conv.Get(),transforms[orientation<=8?orientation:1]))||FAILED(oriented->GetSize(&img.width,&img.height)))return false;
    img.pixels.resize(size_t(img.width)*img.height*4);
    return SUCCEEDED(oriented->CopyPixels(nullptr,img.width*4,static_cast<UINT>(img.pixels.size()),img.pixels.data()));
}
bool saveImage(const std::wstring& path,const RgbaImage& img,bool jpeg) {
    if(img.pixels.size()!=size_t(img.width)*img.height*4||std::filesystem::exists(path))return false;
    const auto temp=path+L".partial";
    if(std::filesystem::exists(temp))return false;
    ComPtr<IWICImagingFactory> fac;ComPtr<IWICStream> stream;ComPtr<IWICBitmapEncoder> enc;ComPtr<IWICBitmapFrameEncode> frame;ComPtr<IPropertyBag2> opts;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&fac)))||
       FAILED(fac->CreateStream(&stream))||FAILED(stream->InitializeFromFilename(temp.c_str(),GENERIC_WRITE))||
       FAILED(fac->CreateEncoder(jpeg?GUID_ContainerFormatJpeg:GUID_ContainerFormatPng,nullptr,&enc))||
       FAILED(enc->Initialize(stream.Get(),WICBitmapEncoderNoCache))||FAILED(enc->CreateNewFrame(&frame,&opts))||
       FAILED(frame->Initialize(opts.Get()))||FAILED(frame->SetSize(img.width,img.height)))return false;
    auto format=jpeg?GUID_WICPixelFormat24bppBGR:GUID_WICPixelFormat32bppRGBA;
    if(FAILED(frame->SetPixelFormat(&format)))return false;
    std::vector<uint8_t> bgr;
    const uint8_t* pixels=img.pixels.data();UINT stride=img.width*4;
    if(jpeg){bgr.resize(size_t(img.width)*img.height*3);for(size_t i=0;i<img.pixels.size()/4;++i){bgr[i*3]=pixels[i*4+2];bgr[i*3+1]=pixels[i*4+1];bgr[i*3+2]=pixels[i*4];}pixels=bgr.data();stride=img.width*3;}
    if(FAILED(frame->WritePixels(img.height,stride,stride*img.height,const_cast<BYTE*>(pixels)))||FAILED(frame->Commit())||FAILED(enc->Commit()))return false;
    frame.Reset();enc.Reset();stream.Reset();RgbaImage check;
    if(!loadImage(temp,check)||check.width!=img.width||check.height!=img.height)return false;
    return MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE;
}
}
