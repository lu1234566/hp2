#include "hp2/ue_actor.h"
#include "hp2/ue_package.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
constexpr std::int32_t kMax = 2000000;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){return static_cast<char>(std::tolower(c));});
    return s;
}

std::filesystem::path findPackage(const std::filesystem::path& root, const std::string& stem) {
    std::error_code ec;
    const auto opts = std::filesystem::directory_options::skip_permission_denied;
    for (std::filesystem::recursive_directory_iterator it(root, opts, ec), end; it != end;) {
        if (ec) { ec.clear(); it.increment(ec); continue; }
        if (it->is_regular_file(ec) && !ec && hp2::IsPackageExtension(it->path())
            && lower(it->path().stem().string()) == lower(stem)) return it->path();
        it.increment(ec);
    }
    return {};
}

bool compactAt(const std::vector<std::uint8_t>& b, std::size_t p, std::int32_t& out, std::size_t& used) {
    if (p >= b.size()) return false;
    const auto first=b[p]; bool neg=first&0x80u; std::uint32_t v=first&0x3fu; bool more=first&0x40u; used=1; std::uint32_t shift=6;
    while (more) {
        if (used>=5 || p+used>=b.size() || shift>=32) return false;
        const auto x=b[p+used]; const std::uint32_t payload=x&0x7fu;
        if (shift==27 && payload>0x0fu) return false;
        v|=payload<<shift; more=x&0x80u; shift+=7; ++used;
    }
    if (v>static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) return false;
    out=neg?-static_cast<std::int32_t>(v):static_cast<std::int32_t>(v); return true;
}

class Cur {
public:
    Cur(const std::vector<std::uint8_t>& b,std::size_t p=0):b_(b),p_(p){}
    std::size_t pos()const{return p_;} std::size_t rem()const{return p_<=b_.size()?b_.size()-p_:0;}
    bool compact(std::int32_t& v){std::size_t u=0;if(!compactAt(b_,p_,v,u))return false;p_+=u;return true;}
    bool skip(std::size_t n){if(n>rem())return false;p_+=n;return true;}
private: const std::vector<std::uint8_t>& b_; std::size_t p_;
};

bool count(Cur& c,std::int32_t& n,std::int32_t max=kMax){return c.compact(n)&&n>=0&&n<=max;}

std::vector<std::uint8_t> native(const hp2::PackageIndex& pkg,const std::string& name){
    for(std::size_t i=0;i<pkg.exports.size();++i){const auto&e=pkg.exports[i];if(lower(e.class_name)!="animation"||lower(e.object_name)!=lower(name))continue;
        const auto props=hp2::LoadObjectProperties(pkg,i);if(!props.valid||e.serial_size<=0||e.serial_offset<0)return{};
        std::ifstream f(pkg.summary.path,std::ios::binary);f.seekg(e.serial_offset);std::vector<std::uint8_t> p(static_cast<std::size_t>(e.serial_size));
        f.read(reinterpret_cast<char*>(p.data()),static_cast<std::streamsize>(p.size()));if(f.gcount()!=static_cast<std::streamsize>(p.size())||props.native_data_offset>p.size())return{};
        return {p.begin()+static_cast<std::ptrdiff_t>(props.native_data_offset),p.end()};}
    return{};
}

std::size_t movesOffset(const std::vector<std::uint8_t>& b){Cur c(b);std::int32_t n=0;if(!count(c,n,4096))return 0;for(int i=0;i<n;++i){std::int32_t x=0;if(!c.compact(x)||!c.skip(8))return 0;}return c.pos();}

struct R{std::size_t q=0,p=0,t=0;bool valid=false;std::string error;int moves=0,doneMoves=0,failMove=-1,failTrack=-1;std::uint64_t doneTracks=0,qKeys=0,pKeys=0,tKeys=0;std::size_t off=0,end=0,remain=0;int boneMin=999999,boneMax=0,trackMin=999999,trackMax=0;};

bool track(Cur&c,R&r){
    if(!c.skip(4)){r.error="flags";return false;}
    std::int32_t n=0;if(!count(c,n)){r.error="q_count";return false;}auto bytes=static_cast<std::uint64_t>(n)*r.q;if(bytes>c.rem()||!c.skip(static_cast<std::size_t>(bytes))){r.error="q_data";return false;}r.qKeys+=n;
    if(!count(c,n)){r.error="p_count";return false;}bytes=static_cast<std::uint64_t>(n)*r.p;if(bytes>c.rem()||!c.skip(static_cast<std::size_t>(bytes))){r.error="p_data";return false;}r.pKeys+=n;
    if(!count(c,n)){r.error="t_count";return false;}bytes=static_cast<std::uint64_t>(n)*r.t;if(bytes>c.rem()||!c.skip(static_cast<std::size_t>(bytes))){r.error="t_data";return false;}r.tKeys+=n;
    ++r.doneTracks;return true;
}

R eval(const std::vector<std::uint8_t>&b,std::size_t start,std::size_t q,std::size_t p,std::size_t t){R r;r.q=q;r.p=p;r.t=t;Cur c(b,start);if(!count(c,r.moves,4096)){r.error="moves";r.off=c.pos();return r;}
    for(int m=0;m<r.moves;++m){r.failMove=m;if(!c.skip(24)){r.error="header";r.off=c.pos();return r;}std::int32_t bones=0;if(!count(c,bones,4096)){r.error="bone_count";r.off=c.pos();return r;}r.boneMin=std::min(r.boneMin,bones);r.boneMax=std::max(r.boneMax,bones);if(!c.skip(static_cast<std::size_t>(bones)*4u)){r.error="bones";r.off=c.pos();return r;}
        std::int32_t tracks=0;if(!count(c,tracks,4096)){r.error="track_count";r.off=c.pos();return r;}r.trackMin=std::min(r.trackMin,tracks);r.trackMax=std::max(r.trackMax,tracks);
        for(int a=0;a<tracks;++a){r.failTrack=a;if(!track(c,r)){r.off=c.pos();return r;}}
        r.failTrack=-1;if(!track(c,r)){r.error="root_"+r.error;r.off=c.pos();return r;}++r.doneMoves;}
    r.valid=true;r.failMove=-1;r.failTrack=-1;r.end=c.pos();r.remain=c.rem();if(r.boneMin==999999)r.boneMin=0;if(r.trackMin==999999)r.trackMin=0;return r;}
}

int main(int argc,char**argv){if(argc!=4)return 64;auto path=findPackage(argv[1],argv[2]);if(path.empty())return 2;auto pkg=hp2::LoadPackageIndex(path);if(!pkg.valid)return 2;auto b=native(pkg,argv[3]);if(b.empty())return 2;auto start=movesOffset(b);if(!start)return 2;
    const std::vector<std::size_t> qs={6,8,10,12,16},ps={6,8,12,16},ts={1,2,4};std::vector<R> all;for(auto q:qs)for(auto p:ps)for(auto t:ts)all.push_back(eval(b,start,q,p,t));
    std::sort(all.begin(),all.end(),[](const R&a,const R&b){if(a.valid!=b.valid)return a.valid>b.valid;if(a.doneMoves!=b.doneMoves)return a.doneMoves>b.doneMoves;if(a.doneTracks!=b.doneTracks)return a.doneTracks>b.doneTracks;return a.off>b.off;});
    const auto n=std::min<std::size_t>(20,all.size());std::cout<<"{\n  \"schema\":\"hp2-packed-after-boneindices-v1\",\n  \"native_bytes\":"<<b.size()<<",\n  \"moves_offset\":"<<start<<",\n  \"top\":[\n";
    for(std::size_t i=0;i<n;++i){const auto&r=all[i];std::cout<<"    {\"q\":"<<r.q<<",\"p\":"<<r.p<<",\"t\":"<<r.t<<",\"valid\":"<<(r.valid?"true":"false")<<",\"moves\":"<<r.moves<<",\"done_moves\":"<<r.doneMoves<<",\"done_tracks\":"<<r.doneTracks<<",\"fail_move\":"<<r.failMove<<",\"fail_track\":"<<r.failTrack<<",\"error\":\""<<r.error<<"\",\"offset\":"<<r.off<<",\"end\":"<<r.end<<",\"remaining\":"<<r.remain<<",\"bone_min\":"<<r.boneMin<<",\"bone_max\":"<<r.boneMax<<",\"track_min\":"<<r.trackMin<<",\"track_max\":"<<r.trackMax<<",\"q_keys\":"<<r.qKeys<<",\"p_keys\":"<<r.pKeys<<",\"t_keys\":"<<r.tKeys<<"}"<<(i+1==n?"":",")<<"\n";}
    std::cout<<"  ]\n}\n";return 0;}
