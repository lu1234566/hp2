#include "hp2/ue_actor.h"
#include "hp2/ue_package.h"

#include <algorithm>
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
constexpr std::int32_t kMaxCount = 4096;

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
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
    explicit Cursor(const std::vector<std::uint8_t>& bytes, std::size_t position = 0u)
        : bytes_(bytes), position_(position) {}
    std::size_t Position() const { return position_; }
    std::size_t Remaining() const { return position_ <= bytes_.size() ? bytes_.size() - position_ : 0u; }
    bool Seek(std::size_t position) { if (position > bytes_.size()) return false; position_ = position; return true; }
    bool Skip(std::size_t count) { if (count > Remaining()) return false; position_ += count; return true; }
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
    bool I32(std::int32_t& value) { std::uint32_t raw = 0; if (!U32(raw)) return false; value = static_cast<std::int32_t>(raw); return true; }
    bool F32(float& value) { std::uint32_t raw = 0; if (!U32(raw)) return false; std::memcpy(&value, &raw, sizeof(value)); return true; }
private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t position_ = 0u;
};

bool Count(Cursor& cursor, std::int32_t& value, std::int32_t max_count = kMaxCount) {
    return cursor.Compact(value) && value >= 0 && value <= max_count;
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

std::vector<std::uint8_t> NativePayload(const hp2::PackageIndex& package, const std::string& object_name) {
    for (std::size_t index = 0; index < package.exports.size(); ++index) {
        const auto& entry = package.exports[index];
        if (Lower(entry.class_name) != "animation" || Lower(entry.object_name) != Lower(object_name)) continue;
        const auto properties = hp2::LoadObjectProperties(package, index);
        if (!properties.valid || entry.serial_size <= 0 || entry.serial_offset < 0) return {};
        std::ifstream input(package.summary.path, std::ios::binary);
        if (!input) return {};
        input.seekg(entry.serial_offset, std::ios::beg);
        std::vector<std::uint8_t> payload(static_cast<std::size_t>(entry.serial_size));
        input.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
        if (input.gcount() != static_cast<std::streamsize>(payload.size())
            || properties.native_data_offset > payload.size()) return {};
        return {payload.begin() + static_cast<std::ptrdiff_t>(properties.native_data_offset), payload.end()};
    }
    return {};
}

struct QuatStats {
    std::int32_t count = 0;
    std::int32_t finite = 0;
    std::int32_t near_unit = 0;
    double norm_min = std::numeric_limits<double>::infinity();
    double norm_max = 0.0;
};

bool ReadQuatArray(Cursor& cursor, QuatStats& stats) {
    if (!Count(cursor, stats.count)) return false;
    for (std::int32_t i = 0; i < stats.count; ++i) {
        float x = 0, y = 0, z = 0, w = 0;
        if (!cursor.F32(x) || !cursor.F32(y) || !cursor.F32(z) || !cursor.F32(w)) return false;
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(w)) continue;
        ++stats.finite;
        const double norm = std::sqrt(static_cast<double>(x) * x + static_cast<double>(y) * y
            + static_cast<double>(z) * z + static_cast<double>(w) * w);
        stats.norm_min = std::min(stats.norm_min, norm);
        stats.norm_max = std::max(stats.norm_max, norm);
        if (norm >= 0.90 && norm <= 1.10) ++stats.near_unit;
    }
    if (!std::isfinite(stats.norm_min)) stats.norm_min = 0.0;
    return true;
}

struct VecStats {
    std::int32_t count = 0;
    std::int32_t finite = 0;
    double abs_max = 0.0;
};

bool ReadVecArray(Cursor& cursor, VecStats& stats) {
    if (!Count(cursor, stats.count)) return false;
    for (std::int32_t i = 0; i < stats.count; ++i) {
        float x = 0, y = 0, z = 0;
        if (!cursor.F32(x) || !cursor.F32(y) || !cursor.F32(z)) return false;
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) continue;
        ++stats.finite;
        stats.abs_max = std::max(stats.abs_max, std::fabs(static_cast<double>(x)));
        stats.abs_max = std::max(stats.abs_max, std::fabs(static_cast<double>(y)));
        stats.abs_max = std::max(stats.abs_max, std::fabs(static_cast<double>(z)));
    }
    return true;
}

struct TimeStats {
    std::int32_t count = 0;
    std::int32_t finite = 0;
    std::int32_t nonnegative = 0;
    double min = std::numeric_limits<double>::infinity();
    double max = -std::numeric_limits<double>::infinity();
};

bool ReadTimeArray(Cursor& cursor, TimeStats& stats) {
    if (!Count(cursor, stats.count)) return false;
    for (std::int32_t i = 0; i < stats.count; ++i) {
        float value = 0;
        if (!cursor.F32(value)) return false;
        if (!std::isfinite(value)) continue;
        ++stats.finite;
        if (value >= 0.0f) ++stats.nonnegative;
        stats.min = std::min(stats.min, static_cast<double>(value));
        stats.max = std::max(stats.max, static_cast<double>(value));
    }
    if (!std::isfinite(stats.min)) { stats.min = 0.0; stats.max = 0.0; }
    return true;
}

struct TrackStats {
    bool valid = false;
    std::uint32_t flags = 0;
    QuatStats q;
    VecStats p;
    TimeStats t;
    std::size_t start = 0;
    std::size_t end = 0;
    std::string error;
};

TrackStats ReadStandardTrack(Cursor& cursor) {
    TrackStats result;
    result.start = cursor.Position();
    if (!cursor.U32(result.flags)) { result.error = "flags"; return result; }
    if (!ReadQuatArray(cursor, result.q)) { result.error = "quat"; return result; }
    if (!ReadVecArray(cursor, result.p)) { result.error = "pos"; return result; }
    if (!ReadTimeArray(cursor, result.t)) { result.error = "time"; return result; }
    result.valid = true;
    result.end = cursor.Position();
    return result;
}

struct BoundaryCandidate {
    int q_stride = 0;
    int delta = 0;
    std::int32_t p_count = 0;
    std::int32_t t_count = 0;
    std::int32_t p_finite = 0;
    std::int32_t t_finite = 0;
    bool plausible = false;
};

BoundaryCandidate TestBoundary(const std::vector<std::uint8_t>& bytes, std::size_t q_data_start,
                               std::int32_t q_count, int q_stride, int delta) {
    BoundaryCandidate out;
    out.q_stride = q_stride;
    out.delta = delta;
    const long long signed_pos = static_cast<long long>(q_data_start)
        + static_cast<long long>(q_count) * q_stride + delta;
    if (signed_pos < 0 || static_cast<std::size_t>(signed_pos) >= bytes.size()) return out;
    Cursor cursor(bytes, static_cast<std::size_t>(signed_pos));
    VecStats p;
    if (!ReadVecArray(cursor, p) || p.count > 256 || p.finite != p.count || p.abs_max > 1.0e7) return out;
    TimeStats t;
    if (!ReadTimeArray(cursor, t) || t.count > 512 || t.finite != t.count) return out;
    out.p_count = p.count;
    out.t_count = t.count;
    out.p_finite = p.finite;
    out.t_finite = t.finite;
    out.plausible = true;
    return out;
}

void PrintQuat(const QuatStats& q) {
    std::cout << "{\"count\":" << q.count << ",\"finite\":" << q.finite
              << ",\"near_unit\":" << q.near_unit << ",\"norm_min\":" << q.norm_min
              << ",\"norm_max\":" << q.norm_max << "}";
}
void PrintVec(const VecStats& p) {
    std::cout << "{\"count\":" << p.count << ",\"finite\":" << p.finite
              << ",\"abs_max\":" << p.abs_max << "}";
}
void PrintTime(const TimeStats& t) {
    std::cout << "{\"count\":" << t.count << ",\"finite\":" << t.finite
              << ",\"nonnegative\":" << t.nonnegative << ",\"min\":" << t.min
              << ",\"max\":" << t.max << "}";
}
}

int main(int argc, char** argv) {
    if (argc != 4) return 64;
    const auto package_path = FindPackage(argv[1], argv[2]);
    if (package_path.empty()) return 2;
    const auto package = hp2::LoadPackageIndex(package_path);
    if (!package.valid) return 2;
    const auto bytes = NativePayload(package, argv[3]);
    if (bytes.empty()) return 2;

    Cursor cursor(bytes);
    std::int32_t bones = 0;
    if (!Count(cursor, bones, 4096)) return 3;
    for (std::int32_t i = 0; i < bones; ++i) {
        std::int32_t name = 0, parent = 0;
        std::uint32_t flags = 0;
        if (!cursor.Compact(name) || !cursor.U32(flags) || !cursor.I32(parent)) return 3;
    }
    const std::size_t motion_start = cursor.Position();

    std::int32_t moves = 0;
    if (!Count(cursor, moves, 4096)) return 3;
    float root_x = 0, root_y = 0, root_z = 0, track_time = 0;
    std::int32_t start_bone = 0;
    std::uint32_t move_flags = 0;
    if (!cursor.F32(root_x) || !cursor.F32(root_y) || !cursor.F32(root_z) || !cursor.F32(track_time)
        || !cursor.I32(start_bone) || !cursor.U32(move_flags)) return 3;

    std::int32_t bone_indices = 0;
    if (!Count(cursor, bone_indices, 4096)) return 3;
    std::int32_t bone_index_in_range = 0, bone_index_identity = 0;
    for (std::int32_t i = 0; i < bone_indices; ++i) {
        std::int32_t value = 0;
        if (!cursor.I32(value)) return 3;
        if (value >= -1 && value < bones) ++bone_index_in_range;
        if (value == i) ++bone_index_identity;
    }
    std::int32_t tracks = 0;
    if (!Count(cursor, tracks, 4096)) return 3;
    const std::size_t tracks_start = cursor.Position();

    TrackStats first = ReadStandardTrack(cursor);
    const std::size_t second_start = cursor.Position();

    TrackStats second_prefix;
    second_prefix.start = second_start;
    std::size_t q_data_start = 0;
    if (cursor.U32(second_prefix.flags) && Count(cursor, second_prefix.q.count, 4096)) {
        q_data_start = cursor.Position();
        for (std::int32_t i = 0; i < second_prefix.q.count; ++i) {
            float x = 0, y = 0, z = 0, w = 0;
            if (!cursor.F32(x) || !cursor.F32(y) || !cursor.F32(z) || !cursor.F32(w)) break;
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(w)) continue;
            ++second_prefix.q.finite;
            const double norm = std::sqrt(static_cast<double>(x) * x + static_cast<double>(y) * y
                + static_cast<double>(z) * z + static_cast<double>(w) * w);
            second_prefix.q.norm_min = std::min(second_prefix.q.norm_min, norm);
            second_prefix.q.norm_max = std::max(second_prefix.q.norm_max, norm);
            if (norm >= 0.90 && norm <= 1.10) ++second_prefix.q.near_unit;
        }
        if (!std::isfinite(second_prefix.q.norm_min)) second_prefix.q.norm_min = 0.0;
    }

    std::vector<BoundaryCandidate> candidates;
    if (q_data_start != 0u && second_prefix.q.count >= 0 && second_prefix.q.count <= 256) {
        const int strides[] = {4, 6, 8, 10, 12, 14, 16, 20, 24, 28, 32};
        for (int stride : strides) {
            for (int delta = -8; delta <= 8; ++delta) {
                auto candidate = TestBoundary(bytes, q_data_start, second_prefix.q.count, stride, delta);
                if (candidate.plausible) candidates.push_back(candidate);
            }
        }
    }

    std::cout << "{\n"
              << "  \"schema\":\"hp2-animation-semantic-probe-v1\",\n"
              << "  \"file_version\":" << package.summary.file_version << ",\n"
              << "  \"licensee_version\":" << package.summary.licensee_version << ",\n"
              << "  \"native_bytes\":" << bytes.size() << ",\n"
              << "  \"refbones\":" << bones << ",\n"
              << "  \"motion_start\":" << motion_start << ",\n"
              << "  \"moves\":" << moves << ",\n"
              << "  \"move0_track_time_finite\":" << (std::isfinite(track_time) ? "true" : "false") << ",\n"
              << "  \"move0_start_bone\":" << start_bone << ",\n"
              << "  \"move0_flags_nonzero\":" << (move_flags != 0u ? "true" : "false") << ",\n"
              << "  \"bone_indices\":" << bone_indices << ",\n"
              << "  \"bone_indices_in_range\":" << bone_index_in_range << ",\n"
              << "  \"bone_indices_identity\":" << bone_index_identity << ",\n"
              << "  \"anim_tracks\":" << tracks << ",\n"
              << "  \"tracks_start\":" << tracks_start << ",\n"
              << "  \"track0\":{\"valid\":" << (first.valid ? "true" : "false")
              << ",\"flags_nonzero\":" << (first.flags != 0u ? "true" : "false") << ",\"quat\":";
    PrintQuat(first.q);
    std::cout << ",\"pos\":"; PrintVec(first.p);
    std::cout << ",\"time\":"; PrintTime(first.t);
    std::cout << ",\"start\":" << first.start << ",\"end\":" << first.end << "},\n"
              << "  \"track1_prefix\":{\"flags_nonzero\":" << (second_prefix.flags != 0u ? "true" : "false")
              << ",\"quat\":";
    PrintQuat(second_prefix.q);
    std::cout << ",\"start\":" << second_start << ",\"q_data_start\":" << q_data_start << "},\n"
              << "  \"boundary_candidates\":[\n";
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const auto& c = candidates[i];
        std::cout << "    {\"q_stride\":" << c.q_stride << ",\"delta\":" << c.delta
                  << ",\"p_count\":" << c.p_count << ",\"t_count\":" << c.t_count << "}"
                  << (i + 1u == candidates.size() ? "" : ",") << '\n';
    }
    std::cout << "  ]\n}\n";
    return 0;
}
