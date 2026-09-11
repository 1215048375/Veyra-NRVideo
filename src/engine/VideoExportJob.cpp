#include "veyra/engine/VideoExportJob.h"
#include "veyra/engine/CfrTimeline.h"
#include "veyra/source/MediaFileSource.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/pipeline/ResolutionPlan.h"
#include "veyra/sink/NvencD3D12Encoder.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/RuntimePaths.h"
#include <filesystem>
#include <format>
#include <thread>
#include <chrono>
#include <cmath>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/mathematics.h>
}
namespace veyra::engine {
namespace { std::string utf8(const std::wstring& s){const int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string r(n,0);WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),r.data(),n,nullptr,nullptr);return r;} }
bool exportVideo(const std::wstring& input,const std::wstring& output,PlayerOptions options,bool hevc,std::atomic<bool>& cancel,const std::function<void(double,const std::wstring&)>& progress,unsigned maxFrames,const std::function<bool()>& frameBoundary,const std::function<void(const ExportCounts&)>& counts){
    if(std::filesystem::exists(output)||std::filesystem::exists(output+L".partial")){progress(0,L"目标或partial文件已存在，请使用其他名称");return false;}
    if(options.fg&&options.settings.frameGenerationBackend==FrameGenerationBackend::XeSS){
        progress(0,L"XeSS 帧生成目前仅支持预览；导出请选择 DLSS 或关闭补帧");
        veyra::log::warn("export","XeSS FG export rejected: public XeSS swapchain API has no encoder texture output contract");
        return false;
    }
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;source::MediaFileSource source;pipeline::EnhanceGraph graph(ctx,ring);sink::NvencD3D12Encoder enc;
    AVFormatContext *mux=nullptr,*audioInput=nullptr;AVStream* videoStream=nullptr;AVStream* audioStream=nullptr;AVPacket* audioPacket=av_packet_alloc();
    int audioIndex=-1;bool audioPending=false,audioEof=false,ok=false,headerWritten=false;int64_t written=0;double audioEndSeconds=0,videoOriginSeconds=0;
    const auto partial=output+L".partial";
    try { do {
        Status st=Status::Ok;gfx::DeviceContextDesc dd;dd.commandSlotCount=6;
        if(!ctx.initialize(dd,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,st))break;
        if(!ctx.adapter().isNvidia){
            progress(0,L"当前视频导出使用 NVIDIA NVENC；尚未实现 AMD AMF 或 Intel 编码后端");
            veyra::log::warn("export","export rejected: selected adapter has no NVIDIA NVENC; AMD AMF and Intel encoder backends are not implemented");
            break;
        }
        source::SourceOpenDesc od;od.path=input;od.preferHardwareDecode=false;if(!source.open(od))break;
        const auto info=source.info();if(!pipeline::Extent{info.width,info.height}.valid()){progress(0,L"输入尺寸超出GPU单纹理能力");break;}
        // Bounded metadata scan; decoded pixels are not retained. Rewind the
        // file source afterwards, preserving all source frames for the export.
        std::vector<double> samples;bool scanError=false;
        for(unsigned i=0;i<120&&!cancel;++i){pipeline::FramePacket p;const AVFrame* f=nullptr;const auto rs=source.read(p,&f);if(rs==source::SourceReadStatus::Eos)break;if(rs!=source::SourceReadStatus::Frame||p.pts.isUnknown()){scanError=true;break;}samples.push_back(p.pts.toDouble());}
        if(scanError||samples.empty()||cancel)break;
        const auto [rateNum,rateDen]=CfrTimeline::select(info.nominalRateNum,info.nominalRateDen,info.timestampQuantum,samples);
        CfrTimeline timeline(rateNum,rateDen,info.timestampQuantum,samples.front());
        if(!timeline.valid()){progress(0,L"无法确认一致的恒定帧率，已停止导出");veyra::log::error("export-timeline","CFR rejected source=preflight; no timestamp-consistent rate candidate");break;}
        source.close();if(!source.open(od))break; // reopen from the true beginning, including negative PTS
        const AVRational rate=av_mul_q({rateNum,rateDen},{int(options.fg?options.fgMultiplier:1),1});
        veyra::log::info("export-timeline",std::format("CFR declared={}/{} candidate={}/{} timestampQuantum={} sampled={} output={}/{} (timestamp-consistent candidate, quantized short clips may be ambiguous; every PTS validated)",info.nominalRateNum,info.nominalRateDen,rateNum,rateDen,info.timestampQuantum,samples.size(),rate.num,rate.den));
        const auto resolution=pipeline::ResolutionPlan::make({info.width,info.height},options.sr,pipeline::NrSizePolicy::Native,true,options.settings.revision,options.settings.srTarget);
        pipeline::EnhanceGraphDesc gd;gd.sourceWidth=info.width;gd.sourceHeight=info.height;gd.workWidth=resolution.base.width;gd.workHeight=resolution.base.height;gd.nrWidth=resolution.nr.width;gd.nrHeight=resolution.nr.height;gd.flowWidth=resolution.flow.width;gd.flowHeight=resolution.flow.height;gd.enableSr=resolution.srApplied;gd.videoSrQuality=options.settings.videoSrQuality;gd.enableNr=options.nr;gd.nrRuntime=options.settings.nrRuntime;gd.enableFg=options.fg;gd.fgMultiplier=options.fgMultiplier;gd.frameGenerationBackend=options.settings.frameGenerationBackend;gd.enableNvofStandalone=options.nr;gd.model=options.settings.model;gd.residual=options.settings.residual;gd.protection=options.settings.protection;gd.settingsRevision=options.settings.revision;gd.flowQuality=options.settings.flow;gd.opticalFlowBackend=options.settings.opticalFlowBackend;gd.amdFlowHalfResolution=options.settings.amdFlowHalfResolution;gd.contentRate=options.settings.content;gd.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
        if(!graph.initialize(gd)||!graph.createViews())break;
        if(avformat_alloc_output_context2(&mux,nullptr,"mp4",utf8(partial).c_str())<0||!mux)break;
        videoStream=avformat_new_stream(mux,nullptr);if(!videoStream)break;videoStream->time_base={rate.den,rate.num};videoStream->avg_frame_rate=rate;
        auto* cp=videoStream->codecpar;cp->codec_type=AVMEDIA_TYPE_VIDEO;cp->codec_id=hevc?AV_CODEC_ID_HEVC:AV_CODEC_ID_H264;cp->width=gd.workWidth;cp->height=gd.workHeight;cp->format=AV_PIX_FMT_YUV420P;cp->color_range=AVCOL_RANGE_MPEG;cp->color_space=AVCOL_SPC_BT709;cp->color_primaries=AVCOL_PRI_BT709;cp->color_trc=AVCOL_TRC_IEC61966_2_1;
        auto inputUtf8=utf8(input);
        if(avformat_open_input(&audioInput,inputUtf8.c_str(),nullptr,nullptr)<0||avformat_find_stream_info(audioInput,nullptr)<0){progress(0,L"无法读取源音轨信息，已停止导出");break;}
        {
            audioIndex=av_find_best_stream(audioInput,AVMEDIA_TYPE_AUDIO,-1,-1,nullptr,0);
            if(audioIndex>=0){auto* acp=audioInput->streams[audioIndex]->codecpar;
                if(avformat_query_codec(mux->oformat,acp->codec_id,FF_COMPLIANCE_NORMAL)<=0){progress(0,L"此音频编码不能封装为MP4，已停止导出");break;}
                audioStream=avformat_new_stream(mux,nullptr);if(!audioStream||avcodec_parameters_copy(audioStream->codecpar,acp)<0)break;audioStream->codecpar->codec_tag=0;audioStream->time_base=audioInput->streams[audioIndex]->time_base;
            }
            av_dict_copy(&mux->metadata,audioInput->metadata,0);
        }
        auto writeAudioUntil=[&](double seconds){if(!audioStream)return true;
            for(;;){if(!audioPending){if(audioEof)return true;av_packet_unref(audioPacket);if(av_read_frame(audioInput,audioPacket)<0){audioEof=true;return true;}if(audioPacket->stream_index!=audioIndex)continue;audioPending=true;}
                const auto tb=audioInput->streams[audioIndex]->time_base;const int64_t ts=audioPacket->pts!=AV_NOPTS_VALUE?audioPacket->pts:audioPacket->dts;const double time=ts==AV_NOPTS_VALUE?0:ts*av_q2d(tb)-videoOriginSeconds;if(time>seconds)return true;
                const int64_t origin=av_rescale_q(static_cast<int64_t>(videoOriginSeconds*1000000),{1,1000000},tb);
                if(audioPacket->pts!=AV_NOPTS_VALUE)audioPacket->pts-=origin;if(audioPacket->dts!=AV_NOPTS_VALUE)audioPacket->dts-=origin;
                av_packet_rescale_ts(audioPacket,tb,audioStream->time_base);audioPacket->stream_index=audioStream->index;audioPacket->pos=-1;audioPending=false;if(av_interleaved_write_frame(mux,audioPacket)<0)return false;
            }};
        auto writer=[&](const uint8_t* bytes,size_t size,int64_t pts,bool key){if(!headerWritten)return false;
            AVPacket* pkt=av_packet_alloc();if(!pkt)return false;if(av_new_packet(pkt,int(size))<0){av_packet_free(&pkt);return false;}memcpy(pkt->data,bytes,size);pkt->stream_index=videoStream->index;pkt->pts=pkt->dts=pts;pkt->duration=1;if(key)pkt->flags|=AV_PKT_FLAG_KEY;
            av_packet_rescale_ts(pkt,{rate.den,rate.num},videoStream->time_base);const bool good=av_interleaved_write_frame(mux,pkt)>=0;av_packet_free(&pkt);++written;audioEndSeconds=(pts+1)*double(rate.den)/rate.num;return good&&writeAudioUntil(audioEndSeconds);};
        // Must drain before rate/writeAudioUntil/writer leave scope, including cancel/error.
        struct EncoderCloser {sink::NvencD3D12Encoder& encoder;~EncoderCloser(){encoder.close();}} closer{enc};
        if(!enc.open(ctx,ring,graph,hevc,rate.num,rate.den,writer))break;
        auto headers=enc.headers();cp->extradata=static_cast<uint8_t*>(av_mallocz(headers.size()+AV_INPUT_BUFFER_PADDING_SIZE));if(!cp->extradata)break;memcpy(cp->extradata,headers.data(),headers.size());cp->extradata_size=int(headers.size());
        if(avio_open(&mux->pb,utf8(partial).c_str(),AVIO_FLAG_WRITE)<0||avformat_write_header(mux,nullptr)<0)break;headerWritten=true;
        uint64_t sourceCount=0,generatedCount=0,holdCount=0;int64_t outputIndex=0;bool error=false;std::shared_ptr<pipeline::FrameLease> lastReal;
        while(!cancel){if(frameBoundary&&!frameBoundary())break;pipeline::FramePacket packet;const AVFrame* frame=nullptr;const auto rs=source.read(packet,&frame);if(rs==source::SourceReadStatus::Eos)break;if(rs!=source::SourceReadStatus::Frame||packet.pts.isUnknown()){error=true;break;}
            if(sourceCount==0)videoOriginSeconds=packet.pts.toDouble();
            const double expectedPts=timeline.expected(sourceCount);
            if(!timeline.accepts(sourceCount,packet.pts.toDouble())){
                progress(0,L"此文件时间戳不均匀，当前CFR导出不支持；已保留partial，未改写音画时间轴");
                veyra::log::error("export-timeline",std::format("CFR rejected source={} pts={} expected={}",sourceCount,packet.pts.toDouble(),expectedPts));error=true;break;
            }
            pipeline::EnhanceGraph::FrameOutputs out;if(!graph.process(frame,packet.pts.toDouble()*1000,sourceCount==0||pipeline::breaksHistory(packet.flags),out,packet.sequence,&packet.colorInfo,false)){error=true;break;}
            const auto readyStart=std::chrono::steady_clock::now();
            while(!cancel&&!graph.resolveGeneration(out)){
                if(std::chrono::steady_clock::now()-readyStart>std::chrono::seconds(2)){error=true;break;}
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if(error||cancel)break;
            if(sourceCount>0&&options.fg){
                for(uint32_t j=1;j<options.fgMultiplier;++j){
                    pipeline::BatchFrame* item=nullptr;
                    for(uint32_t k=0;k<out.batch.count;++k)if(out.batch.frames[k].subframe==j&&out.batch.frames[k].validity==pipeline::GenerationValidity::Valid)item=&out.batch.frames[k];
                    if(item){if(!enc.encode(item->lease->slot,true,outputIndex++)){error=true;break;}item->lease->consumerFence=ring.lastSignaledValue();++generatedCount;}
                    else {if(!lastReal||!enc.encode(lastReal->slot,false,outputIndex++)){error=true;break;}lastReal->consumerFence=ring.lastSignaledValue();++holdCount;}
                }
            }
            if(error)break;
            auto& real=out.batch.frames[out.batch.count-1];
            if(!enc.encode(real.lease->slot,false,outputIndex++)){error=true;break;}
            real.lease->consumerFence=ring.lastSignaledValue();lastReal=real.lease;
            ++sourceCount;if(counts)counts({sourceCount,generatedCount,holdCount,uint64_t(written)});progress(info.duration.toDouble()>0?std::clamp((packet.pts.toDouble()-videoOriginSeconds)/info.duration.toDouble(),0.0,.99):0,std::format(L"正在导出：{}张源帧 / {}张编码帧（D3D12 NVENC）",sourceCount,outputIndex));
            if(maxFrames&&sourceCount>=maxFrames)break;
        }
        if(error||cancel)break;
        if(options.fg&&lastReal)for(uint32_t j=1;j<options.fgMultiplier;++j){if(!enc.encode(lastReal->slot,false,outputIndex++)){error=true;break;}++holdCount;}
        if(error)break;
        veyra::log::info("export-counts",std::format("source={} generated={} hold={} output={} multiplier={} (CFR holds are not DLSSG)",sourceCount,generatedCount,holdCount,outputIndex,options.fg?options.fgMultiplier:1));
        if(!enc.finish()||!writeAudioUntil(audioEndSeconds)||av_write_trailer(mux)<0)break;
        ok=written>0;if(counts)counts({sourceCount,generatedCount,holdCount,uint64_t(written)});
    }while(false); }catch(const std::exception&){progress(0,L"导出异常，已保留partial，请查看诊断");ok=false;}
    enc.close();if(mux){if(mux->pb)avio_closep(&mux->pb);avformat_free_context(mux);}if(audioInput)avformat_close_input(&audioInput);av_packet_free(&audioPacket);
    ring.drainQueue();graph.shutdown();source.close();ring.shutdown();ctx.shutdown();
    if(ok){progress(.999,L"正在封装和验证输出");source::MediaFileSource check;source::SourceOpenDesc od;od.path=partial;od.preferHardwareDecode=false;pipeline::FramePacket pkt;const AVFrame* frame=nullptr;ok=check.open(od)&&check.read(pkt,&frame)==source::SourceReadStatus::Frame;check.close();if(ok)ok=MoveFileExW(partial.c_str(),output.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE;}
    progress(ok?1:0,ok?L"视频导出完成，解码回读成功":cancel?L"导出已取消，partial文件已保留":L"视频导出失败，partial文件已保留，请查看诊断");return ok;
}
}
