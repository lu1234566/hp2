#include "hp2/ue_actor.h"
#include "hp2/ue_package.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
constexpr std::int32_t kMaxCount = 4'000'000;

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string JsonEscape(const std::string& value) {
    std::string out;
    for (char c : value) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: if (static_cast<unsigned char>(c) >= 0x20u) out += c; break;
        }
    }
    return out;
}

std::filesystem::path FindPackage(const std::filesystem::path& root, const std::string& stem) {
    const std::string wanted = Lower(stem);
    std::error_code ec;
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    for (std::filesystem::recursive_directory_iterator it(root, options, ec), end; it != end;) {
        if (ec) { ec.clear(); it.increment(ec); continue; }
        if (it->is_regular_file(ec) && !ec && hp2::IsPackageExtension(it->path())
            && Lower(it->path().stem().string()) == wanted) return it->path();
        it.increment(ec);
    }
    return {};
}

bool DecodeCompact(const std::vector<std::uint8_t>& bytes, std::size_t start,
                   std::int32_t& value, std::size_t& used) {
    if (start >= bytes.size()) return false;
    const std::uint8_t first = bytes[start];
    const bool negative = (first & 0x80u) != 0u;
    std::uint32_t magnitude = first & 0x3fu;
    bool more = (first & 0x40u) != 0u;
    std::uint32_t shift = 6u;
    used = 1u;
    while (more) {
        if (used >= 5u || start + used >= bytes.size() || shift >= 32u) return false;
        const std::uint8_t byte = bytes[start + used];
        const std::uint32_t payload = byte & 0x7fu;
        if (shift == 27u && payload > 0x0fu) return false;
        magnitude |= payload << shift;
        more = (byte & 0x80u) != 0u;
        shift += 7u;
        ++used;
    }
    if (magnitude > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) return false;
    const auto signed_value = static_cast<std::int32_t>(magnitude);
    value = negative ? -signed_value : signed_value;
    return true;
}

class Cursor {
public:
    explicit Cursor(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}
    std::size_t Position() const { return position_; }
    std::size_t Remaining() const { return position_ <= bytes_.size() ? bytes_.size() - position_ : 0u; }
    bool Compact(std::int32_t& value) {
        std::size_t used = 0;
        if (!DecodeCompact(bytes_, position_, value, used)) return false;
        position_ += used;
        return true;
    }
    bool U32(std::uint32_t& value) {
        if (Remaining() < 4u) return false;
        value = static_cast<std::uint32_t>(bytes_[position_])
            | (static_cast<std::uint32_t>(bytes_[position_ + 1u]) << 8u)
            | (static_cast<std::uint32_t>(bytes_[position_ + 2u]) << 16u)
            | (static_cast<std::uint32_t>(bytes_[position_ + 3u]) << 24u);
        position_ += 4u;
        return true;
    }
    bool I32(std::int32_t& value) { std::uint32_t raw=0; if(!U32(raw))return false; value=static_cast<std::int32_t>(raw); return true; }
    bool F32(float& value) { std::uint32_t raw=0; if(!U32(raw))return false; std::memcpy(&value,&raw,sizeof(value)); return true; }
    bool Skip(std::size_t count) { if (count > Remaining()) return false; position_ += count; return true; }
private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t position_ = 0;
};

bool ReadCount(Cursor& cursor, std::int32_t& count, std::int32_t max_count = kMaxCount) {
    return cursor.Compact(count) && count >= 0 && count <= max_count;
}

std::vector<std::uint8_t> LoadNative(const hp2::PackageIndex& package, const std::string& object_name) {
    for (std::size_t index=0; index<package.exports.size(); ++index) {
        const auto& entry=package.exports[index];
        if (Lower(entry.class_name)!="animation" || Lower(entry.object_name)!=Lower(object_name)) continue;
        const auto properties=hp2::LoadObjectProperties(package,index);
        if(!properties.valid || entry.serial_size<=0 || entry.serial_offset<0) return {};
        std::ifstream input(package.summary.path,std::ios::binary); if(!input)return{};
        input.seekg(entry.serial_offset,std::ios::beg);
        std::vector<std::uint8_t> payload(static_cast<std::size_t>(entry.serial_size));
        input.read(reinterpret_cast<char*>(payload.data()),static_cast<std::streamsize>(payload.size()));
        if(input.gcount()!=static_cast<std::streamsize>(payload.size()) || properties.native_data_offset>payload.size())return{};
        return {payload.begin()+static_cast<std::ptrdiff_t>(properties.native_data_offset),payload.end()};
    }
    return {};
}

std::string NameAt(const hp2::PackageIndex& package, std::int32_t index) {
    if(index<0 || static_cast<std::size_t>(index)>=package.names.size()) return {};
    return package.names[static_cast<std::size_t>(index)].value;
}

struct SequenceMeta { std::string name, group; std::int32_t start=0, frames=0; std::size_t notifies=0; float rate=0.0f; };

struct Result {
    bool valid=false; std::string error; std::size_t error_offset=0;
    std::int32_t bones=0,moves=0,sequences=0;
    std::size_t bone_names_valid=0; std::size_t parent_invalid=0;
    std::uint64_t bone_indices_total=0,tracks_total=0;
    std::uint64_t q_requested=0,p_requested=0,d_requested=0;
    std::uint64_t nonzero_track_flags=0;
    std::size_t nonfinite_scales=0;
    float pos_scale_min=std::numeric_limits<float>::infinity(),pos_scale_max=-std::numeric_limits<float>::infinity();
    float time_scale_min=std::numeric_limits<float>::infinity(),time_scale_max=-std::numeric_limits<float>::infinity();
    std::int32_t master_q=0,master_p=0,master_d=0;
    std::size_t end_offset=0,remaining=0;
    std::vector<SequenceMeta> sequence_meta;
};

bool SkipAnimVecArray(Cursor& cursor, std::int32_t& count, std::uint64_t& aggregate) {
    if(!ReadCount(cursor,count))return false;
    const std::uint64_t bytes=static_cast<std::uint64_t>(count)*6u;
    if(bytes>cursor.Remaining() || !cursor.Skip(static_cast<std::size_t>(bytes)))return false;
    aggregate+=static_cast<std::uint64_t>(count);return true;
}

bool SkipByteArray(Cursor& cursor, std::int32_t& count, std::uint64_t& aggregate) {
    if(!ReadCount(cursor,count))return false;
    if(static_cast<std::uint64_t>(count)>cursor.Remaining() || !cursor.Skip(static_cast<std::size_t>(count)))return false;
    aggregate+=static_cast<std::uint64_t>(count);return true;
}

Result Parse(const hp2::PackageIndex& package,const std::vector<std::uint8_t>& native){
    Result r; Cursor c(native);
    if(!ReadCount(c,r.bones,4096)){r.error="refbones_count";r.error_offset=c.Position();return r;}
    for(std::int32_t i=0;i<r.bones;++i){std::int32_t name=-1,parent=-1;std::uint32_t flags=0;
        if(!c.Compact(name)||!c.U32(flags)||!c.I32(parent)){r.error="refbone";r.error_offset=c.Position();return r;}
        r.bone_names_valid+=!NameAt(package,name).empty()?1u:0u;
        if(parent<0 || parent>=r.bones) ++r.parent_invalid;
    }
    if(!ReadCount(c,r.moves,4096)){r.error="moves_count";r.error_offset=c.Position();return r;}
    for(std::int32_t move=0;move<r.moves;++move){
        float x=0,y=0,z=0,track_time=0;std::int32_t start_bone=0;std::uint32_t move_flags=0;
        if(!c.F32(x)||!c.F32(y)||!c.F32(z)||!c.F32(track_time)||!c.I32(start_bone)||!c.U32(move_flags)){
            r.error="motion_header";r.error_offset=c.Position();return r;}
        if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||!std::isfinite(track_time)){
            r.error="motion_header_nonfinite";r.error_offset=c.Position();return r;}
        std::int32_t bone_indices=0;if(!ReadCount(c,bone_indices,4096)){r.error="bone_indices_count";r.error_offset=c.Position();return r;}
        const std::uint64_t bone_bytes=static_cast<std::uint64_t>(bone_indices)*4u;
        if(bone_bytes>c.Remaining()||!c.Skip(static_cast<std::size_t>(bone_bytes))){r.error="bone_indices";r.error_offset=c.Position();return r;}
        r.bone_indices_total+=static_cast<std::uint64_t>(bone_indices);
        std::int32_t tracks=0;if(!ReadCount(c,tracks,4096)){r.error="tracks_count";r.error_offset=c.Position();return r;}
        r.tracks_total+=static_cast<std::uint64_t>(tracks);
        for(std::int32_t track=0;track<tracks;++track){
            std::uint32_t flags=0;if(!c.U32(flags)){r.error="track_flags";r.error_offset=c.Position();return r;}r.nonzero_track_flags+=flags!=0u?1u:0u;
            std::int32_t q=0,p=0,d=0;
            if(!SkipAnimVecArray(c,q,r.q_requested)){r.error="track_quat";r.error_offset=c.Position();return r;}
            if(!SkipAnimVecArray(c,p,r.p_requested)){r.error="track_pos";r.error_offset=c.Position();return r;}
            if(!SkipByteArray(c,d,r.d_requested)){r.error="track_delta";r.error_offset=c.Position();return r;}
            float pos_scale=0,time_scale=0;if(!c.F32(pos_scale)||!c.F32(time_scale)){r.error="track_scales";r.error_offset=c.Position();return r;}
            if(!std::isfinite(pos_scale)||!std::isfinite(time_scale)){++r.nonfinite_scales;} else {
                r.pos_scale_min=std::min(r.pos_scale_min,pos_scale);r.pos_scale_max=std::max(r.pos_scale_max,pos_scale);
                r.time_scale_min=std::min(r.time_scale_min,time_scale);r.time_scale_max=std::max(r.time_scale_max,time_scale);
            }
        }
    }
    if(!ReadCount(c,r.sequences,4096)){r.error="animseq_count";r.error_offset=c.Position();return r;}
    r.sequence_meta.reserve(static_cast<std::size_t>(r.sequences));
    for(std::int32_t i=0;i<r.sequences;++i){SequenceMeta seq;std::int32_t name=-1,group=-1,notify_count=0;
        if(!c.Compact(name)||!c.Compact(group)||!c.I32(seq.start)||!c.I32(seq.frames)||!ReadCount(c,notify_count,100000)){
            r.error="animseq_header";r.error_offset=c.Position();return r;}
        seq.name=NameAt(package,name);seq.group=NameAt(package,group);seq.notifies=static_cast<std::size_t>(notify_count);
        for(std::int32_t n=0;n<notify_count;++n){float time=0;std::int32_t fn=0;if(!c.F32(time)||!c.Compact(fn)){r.error="animseq_notify";r.error_offset=c.Position();return r;}}
        if(!c.F32(seq.rate)||!std::isfinite(seq.rate)){r.error="animseq_rate";r.error_offset=c.Position();return r;}
        r.sequence_meta.push_back(std::move(seq));
    }
    std::uint64_t ignore=0;if(!SkipAnimVecArray(c,r.master_q,ignore)){r.error="master_quat";r.error_offset=c.Position();return r;}
    ignore=0;if(!SkipAnimVecArray(c,r.master_p,ignore)){r.error="master_pos";r.error_offset=c.Position();return r;}
    std::uint64_t dummy=0;if(!SkipByteArray(c,r.master_d,dummy)){r.error="master_delta";r.error_offset=c.Position();return r;}
    r.end_offset=c.Position();r.remaining=c.Remaining();
    r.valid=r.remaining==0u
        && r.master_q>=0 && static_cast<std::uint64_t>(r.master_q)==r.q_requested
        && r.master_p>=0 && static_cast<std::uint64_t>(r.master_p)==r.p_requested
        && r.master_d>=0 && static_cast<std::uint64_t>(r.master_d)==r.d_requested;
    if(!r.valid && r.error.empty())r.error="master_counts_or_tail_mismatch";
    if(!std::isfinite(r.pos_scale_min)){r.pos_scale_min=r.pos_scale_max=0.0f;}
    if(!std::isfinite(r.time_scale_min)){r.time_scale_min=r.time_scale_max=0.0f;}
    return r;
}
}

int main(int argc,char**argv){if(argc!=4)return 64;auto path=FindPackage(argv[1],argv[2]);if(path.empty())return 2;auto package=hp2::LoadPackageIndex(path);if(!package.valid)return 2;auto bytes=LoadNative(package,argv[3]);if(bytes.empty())return 2;auto r=Parse(package,bytes);
    std::cout<<"{\n"
        <<"  \"schema\":\"hp2-animation-exact-probe-v1\",\n"
        <<"  \"valid\":"<<(r.valid?"true":"false")<<",\n"
        <<"  \"error\":\""<<JsonEscape(r.error)<<"\",\n"
        <<"  \"error_offset\":"<<r.error_offset<<",\n"
        <<"  \"native_bytes\":"<<bytes.size()<<",\n"
        <<"  \"refbones\":"<<r.bones<<",\n"
        <<"  \"valid_bone_names\":"<<r.bone_names_valid<<",\n"
        <<"  \"invalid_bone_parents\":"<<r.parent_invalid<<",\n"
        <<"  \"moves\":"<<r.moves<<",\n"
        <<"  \"bone_indices_total\":"<<r.bone_indices_total<<",\n"
        <<"  \"tracks_total\":"<<r.tracks_total<<",\n"
        <<"  \"track_flags_nonzero\":"<<r.nonzero_track_flags<<",\n"
        <<"  \"requested_quat_keys\":"<<r.q_requested<<",\n"
        <<"  \"requested_pos_keys\":"<<r.p_requested<<",\n"
        <<"  \"requested_delta_keys\":"<<r.d_requested<<",\n"
        <<"  \"master_quat_keys\":"<<r.master_q<<",\n"
        <<"  \"master_pos_keys\":"<<r.master_p<<",\n"
        <<"  \"master_delta_keys\":"<<r.master_d<<",\n"
        <<"  \"nonfinite_scales\":"<<r.nonfinite_scales<<",\n"
        <<"  \"pos_scale_min\":"<<r.pos_scale_min<<",\n"
        <<"  \"pos_scale_max\":"<<r.pos_scale_max<<",\n"
        <<"  \"time_scale_min\":"<<r.time_scale_min<<",\n"
        <<"  \"time_scale_max\":"<<r.time_scale_max<<",\n"
        <<"  \"sequence_count\":"<<r.sequences<<",\n"
        <<"  \"end_offset\":"<<r.end_offset<<",\n"
        <<"  \"remaining_bytes\":"<<r.remaining<<",\n"
        <<"  \"sequences\":[\n";
    for(std::size_t i=0;i<r.sequence_meta.size();++i){const auto&s=r.sequence_meta[i];
        std::cout<<"    {\"name\":\""<<JsonEscape(s.name)<<"\",\"group\":\""<<JsonEscape(s.group)<<"\",\"start\":"<<s.start<<",\"frames\":"<<s.frames<<",\"notifies\":"<<s.notifies<<",\"rate\":"<<s.rate<<"}"<<(i+1==r.sequence_meta.size()?"":",")<<"\n";}
    std::cout<<"  ]\n}\n";return r.valid?0:3;}
