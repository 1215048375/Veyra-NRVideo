#include "veyra/engine/VideoExportJob.h"
#include "veyra/source/MediaFileSource.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/sink/NvencD3D12Encoder.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include <filesystem>
#include <format>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/mathematics.h>
}
namespace veyra::engine {
namespace { std::string utf8(const std::wstring& s){const int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string r(n,0);WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),r.data(),n,nullptr,nullptr);return r;} }
bool exportVideo(const std::wstring& input,const std::wstring& output,PlayerOptions options,bool hevc,std::atomic<bool>& cancel,const std::function<void(double,const std::wstring&)>& progress,unsigned maxFrames){
    if(std::filesystem::exists(output)||std::filesystem::exists(output+L".partial")){progress(0,L"Output/partial already exists; choose another name");return false;}
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;source::MediaFileSource source;pipeline::EnhanceGraph graph(ctx,ring);sink::NvencD3D12Encoder enc;
    AVFormatContext *mux=nullptr,*audioInput=nullptr;AVStream* videoStream=nullptr;AVStream* audioStream=nullptr;AVPacket* audioPacket=av_packet_alloc();
    int audioIndex=-1;bool audioPending=false,audioEof=false,ok=false,headerWritten=false;int64_t written=0;double audioEndSeconds=0,videoOriginSeconds=0;
    const auto partial=output+L".partial";
    do {
        Status st=Status::Ok;gfx::DeviceContextDesc dd;dd.commandSlotCount=6;
        if(!ctx.initialize(dd,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,st))break;
        source::SourceOpenDesc od;od.path=input;od.preferHardwareDecode=false;if(!source.open(od))break;
        const auto info=source.info();if(info.width>3840||info.height>2160||!info.width||!info.height)break;
        const double fps=info.averageFps;if(fps<=0||fps>120)break;const AVRational rate=av_d2q(fps*(options.fg?2:1),1001000);
        pipeline::EnhanceGraphDesc gd;gd.sourceWidth=info.width;gd.sourceHeight=info.height;gd.workWidth=options.sr?3840:info.width;gd.workHeight=options.sr?2160:info.height;gd.enableSr=options.sr&&(info.width!=3840||info.height!=2160);gd.enableNr=options.nr;gd.enableFg=options.fg;gd.enableNvofStandalone=options.nr;gd.runtimeAbsPath=(std::filesystem::path(VEYRA_PROJECT_ROOT)/"runtime_local/nvidia").wstring();
        if(!graph.initialize(gd)||!graph.createViews())break;
        if(avformat_alloc_output_context2(&mux,nullptr,"mp4",utf8(partial).c_str())<0||!mux)break;
        videoStream=avformat_new_stream(mux,nullptr);if(!videoStream)break;videoStream->time_base={rate.den,rate.num};videoStream->avg_frame_rate=rate;
        auto* cp=videoStream->codecpar;cp->codec_type=AVMEDIA_TYPE_VIDEO;cp->codec_id=hevc?AV_CODEC_ID_HEVC:AV_CODEC_ID_H264;cp->width=gd.workWidth;cp->height=gd.workHeight;cp->format=AV_PIX_FMT_YUV420P;cp->color_range=AVCOL_RANGE_MPEG;cp->color_space=AVCOL_SPC_BT709;cp->color_primaries=AVCOL_PRI_BT709;cp->color_trc=AVCOL_TRC_IEC61966_2_1;
        auto inputUtf8=utf8(input);
        if(avformat_open_input(&audioInput,inputUtf8.c_str(),nullptr,nullptr)<0||avformat_find_stream_info(audioInput,nullptr)<0){progress(0,L"Cannot inspect source audio; export refused rather than silently losing audio");break;}
        {
            audioIndex=av_find_best_stream(audioInput,AVMEDIA_TYPE_AUDIO,-1,-1,nullptr,0);
            if(audioIndex>=0){auto* acp=audioInput->streams[audioIndex]->codecpar;
                if(avformat_query_codec(mux->oformat,acp->codec_id,FF_COMPLIANCE_NORMAL)<=0){progress(0,L"Audio codec cannot be remuxed into MP4; export refused rather than dropping audio");break;}
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
        uint64_t sourceCount=0;int64_t outputIndex=0;bool error=false;
        while(!cancel){pipeline::FramePacket packet;const AVFrame* frame=nullptr;auto rs=source.read(packet,&frame);if(rs==source::SourceReadStatus::Eos)break;if(rs!=source::SourceReadStatus::Frame||packet.pts.isUnknown()){error=true;break;}
            if(sourceCount==0)videoOriginSeconds=packet.pts.toDouble();
            pipeline::EnhanceGraph::FrameOutputs out;if(!graph.process(frame,packet.pts.toDouble()*1000,sourceCount==0||pipeline::breaksHistory(packet.flags),out)){error=true;break;}
            if(out.hasGenerated&&!enc.encode(out.genSlot,true,outputIndex++)){error=true;break;}
            if(options.fg&&sourceCount>0&&!out.hasGenerated){
                veyra::log::info("export","history reset: hold current source for missing interpolation interval (not a generated frame)");
                if(!enc.encode(out.videoSlot,false,outputIndex++)){error=true;break;}
            }
            if(!enc.encode(out.videoSlot,false,outputIndex++)){error=true;break;}
            ++sourceCount;progress(info.duration.toDouble()>0?packet.pts.toDouble()/info.duration.toDouble():0,std::format(L"Exporting {} source frames / {} encoded frames (D3D12 NVENC)",sourceCount,outputIndex));
            if(maxFrames&&sourceCount>=maxFrames)break;
        }
        if(error||cancel)break;
        if(!enc.finish()||!writeAudioUntil(audioEndSeconds)||av_write_trailer(mux)<0)break;
        ok=written>0;
    }while(false);
    enc.close();if(mux){if(mux->pb)avio_closep(&mux->pb);avformat_free_context(mux);}if(audioInput)avformat_close_input(&audioInput);av_packet_free(&audioPacket);
    ring.drainQueue();graph.shutdown();source.close();ring.shutdown();ctx.shutdown();
    if(ok){source::MediaFileSource check;source::SourceOpenDesc od;od.path=partial;od.preferHardwareDecode=false;pipeline::FramePacket pkt;const AVFrame* frame=nullptr;ok=check.open(od)&&check.read(pkt,&frame)==source::SourceReadStatus::Frame;check.close();if(ok)ok=MoveFileExW(partial.c_str(),output.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE;}
    progress(ok?1:0,ok?L"Video exported and decoded back successfully":cancel?L"Export cancelled; partial retained":L"Video export failed; partial retained, see logs");return ok;
}
}
