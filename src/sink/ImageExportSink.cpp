#include "veyra/sink/ImageExportSink.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/Log.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <cstring>
#include <filesystem>
#include <limits>
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
       FAILED(conv->GetSize(&img.width,&img.height))||img.width==0||img.height==0) return false;
    // WIC CopyPixels has a UINT byte-count contract. Do not substitute an
    // arbitrary 4K/8K edge limit for checked byte arithmetic.
    if(uint64_t(img.width)*img.height>std::numeric_limits<UINT>::max()/4){
        log::error("image","decoded RGBA exceeds WIC CopyPixels buffer capacity");return false;
    }
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
    const uint64_t pixelCount=uint64_t(img.width)*img.height;
    if(!img.width||!img.height||pixelCount>SIZE_MAX/4||img.pixels.size()!=pixelCount*4||std::filesystem::exists(path))return false;
    const auto temp=path+L".partial";
    // Own the partial file exclusively; clean it on failure/exception after
    // WIC releases its handles. Never remove an existing caller's partial.
    HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    CloseHandle(file);
    struct Partial {const std::wstring& path;bool active=true;~Partial(){if(active)DeleteFileW(path.c_str());}} partial{temp};
    ComPtr<IWICImagingFactory> fac;ComPtr<IWICStream> stream;ComPtr<IWICBitmapEncoder> enc;ComPtr<IWICBitmapFrameEncode> frame;ComPtr<IPropertyBag2> opts;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&fac)))||
       FAILED(fac->CreateStream(&stream))||FAILED(stream->InitializeFromFilename(temp.c_str(),GENERIC_WRITE))||
       FAILED(fac->CreateEncoder(jpeg?GUID_ContainerFormatJpeg:GUID_ContainerFormatPng,nullptr,&enc))||
       FAILED(enc->Initialize(stream.Get(),WICBitmapEncoderNoCache))||FAILED(enc->CreateNewFrame(&frame,&opts))||
       FAILED(frame->Initialize(opts.Get()))||FAILED(frame->SetSize(img.width,img.height)))return false;
    auto format=jpeg?GUID_WICPixelFormat24bppBGR:GUID_WICPixelFormat32bppBGRA;
    if(FAILED(frame->SetPixelFormat(&format)))return false;
    if(format!=(jpeg?GUID_WICPixelFormat24bppBGR:GUID_WICPixelFormat32bppBGRA)){
        log::error("image","encoder negotiated an unsupported pixel format");return false;
    }
    const UINT channels=jpeg?3:4;
    if(!img.width||!img.height||img.width>UINT_MAX/channels)return false;
    const UINT stride=img.width*channels;
    const UINT bandHeight=std::min(img.height,std::max(1u,(8u*1024*1024)/stride));
    std::vector<uint8_t> bgr(size_t(stride)*bandHeight);
    for(UINT row=0;row<img.height;){
        const UINT rows=std::min(bandHeight,img.height-row);
        const auto* pixels=img.pixels.data()+size_t(row)*img.width*4;
        for(size_t i=0;i<size_t(rows)*img.width;++i){bgr[i*channels]=pixels[i*4+2];bgr[i*channels+1]=pixels[i*4+1];bgr[i*channels+2]=pixels[i*4];if(!jpeg)bgr[i*4+3]=pixels[i*4+3];}
        if(FAILED(frame->WritePixels(rows,stride,stride*rows,bgr.data())))return false;
        static LONG saveFaultInjected=0;
        if(GetEnvironmentVariableW(L"VEYRA_TEST_LARGE_IMAGE_SAVE_THROW",nullptr,0)&&InterlockedCompareExchange(&saveFaultInjected,1,0)==0)throw std::bad_alloc();
        row+=rows;
    }
    if(FAILED(frame->Commit())||FAILED(enc->Commit()))return false;
    frame.Reset();enc.Reset();stream.Reset();RgbaImage check;
    if(!loadImage(temp,check)||check.width!=img.width||check.height!=img.height)return false;
    const bool moved=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(moved)partial.active=false;
    return moved;
}
}
