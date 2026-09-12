// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/remoteplay/Types.h"
namespace veyra::remoteplay {
NalInfo inspectAnnexB(std::span<const std::uint8_t> b,Codec codec) noexcept {
    NalInfo out; if(codec!=Codec::H264 && codec!=Codec::H265 && codec!=Codec::H265Hdr)return out;
    std::size_t pos=0;bool any=false;
    while(pos+3<=b.size()) {
        std::size_t prefix=0;
        if(b[pos]==0 && b[pos+1]==0){
            if(b[pos+2]==1)prefix=3;
            else if(pos+4<=b.size()&&b[pos+2]==0&&b[pos+3]==1)prefix=4;
        }
        if(!prefix){if(!any && b[pos]!=0)return {}; ++pos;continue;}
        const auto h=pos+prefix;
        if(h>=b.size() || (b[h]&0x80u))return {};
        unsigned type=0;
        if(codec==Codec::H264){
            type=b[h]&31u; if(type==0 || type>23)return {};
            out.hasPicture|=type>=1 && type<=5;out.idr|=type==5;out.hasConfig|=type==7 || type==8;
        }else{
            if(h+1>=b.size() || (b[h+1]&7u)==0)return {}; // nuh_temporal_id_plus1 != 0
            type=(b[h]>>1)&63u;out.hasPicture|=type<=31;
            out.idr|=type==19||type==20;out.hasConfig|=type==32||type==33||type==34;
        }
        any=true;pos=h+(codec==Codec::H264?1u:2u);
    }
    out.valid=any;return out;
}
} // namespace veyra::remoteplay
