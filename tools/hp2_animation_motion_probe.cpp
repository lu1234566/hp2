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

constexpr std::int32_t kMaxCount = 2'000'000;

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::filesystem::path FindPackage(const std::filesystem::path& root, const std::string& stem) {
    const std::string wanted = Lower(stem);
    std::error_code ec;
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    for (std::filesystem::recursive_directory_iterator it(root, options, ec), end; it != end;) {
        if (ec) {
            ec.clear();
            it.increment(ec);
            continue;
        }
        if (it->is_regular_file(ec) && !ec && hp2::IsPackageExtension(it->path())
            && Lower(it->path().stem().string()) == wanted) {
            return it->path();
        }
        it.increment(ec);
    }
    return {};
}

bool DecodeCompact(
    const std::vector<std::uint8_t>& bytes,
    std::size_t start,
    std::int32_t& value,
    std::size_t& used
) {
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
    Cursor(const std::vector<std::uint8_t>& bytes, std::size_t position = 0)
        : bytes_(bytes), position_(position) {}

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

    bool I32(std::int32_t& value) {
        std::uint32_t raw = 0;
        if (!U32(raw)) return false;
        value = static_cast<std::int32_t>(raw);
        return true;
    }

    bool F32(float& value) {
        std::uint32_t raw = 0;
        if (!U32(raw)) return false;
        std::memcpy(&value, &raw, sizeof(value));
        return true;
    }

    bool Skip(std::size_t count) {
        if (count > Remaining()) return false;
        position_ += count;
        return true;
    }

private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t position_ = 0;
};

bool ReadCount(Cursor& cursor, std::int32_t& count, std::int32_t max_count = kMaxCount) {
    return cursor.Compact(count) && count >= 0 && count <= max_count;
}

std::vector<std::uint8_t> LoadAnimationNative(
    const hp2::PackageIndex& package,
    const std::string& object_name
) {
    for (std::size_t index = 0; index < package.exports.size(); ++index) {
        const auto& entry = package.exports[index];
        if (Lower(entry.class_name) != "animation" || Lower(entry.object_name) != Lower(object_name)) continue;
        const hp2::ObjectProperties properties = hp2::LoadObjectProperties(package, index);
        if (!properties.valid || entry.serial_size <= 0 || entry.serial_offset < 0) return {};
        std::ifstream input(package.summary.path, std::ios::binary);
        if (!input) return {};
        input.seekg(entry.serial_offset, std::ios::beg);
        std::vector<std::uint8_t> payload(static_cast<std::size_t>(entry.serial_size));
        input.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
        if (input.gcount() != static_cast<std::streamsize>(payload.size())
            || properties.native_data_offset > payload.size()) return {};
        return std::vector<std::uint8_t>(
            payload.begin() + static_cast<std::ptrdiff_t>(properties.native_data_offset), payload.end()
        );
    }
    return {};
}

std::size_t FindMovesOffset(const std::vector<std::uint8_t>& native) {
    Cursor cursor(native);
    std::int32_t bones = 0;
    if (!ReadCount(cursor, bones, 4096)) return 0u;
    for (std::int32_t index = 0; index < bones; ++index) {
        std::int32_t name_index = 0;
        if (!cursor.Compact(name_index) || !cursor.Skip(8u)) return 0u;
    }
    return cursor.Position();
}

struct TrackStats {
    std::uint64_t tracks = 0;
    std::uint64_t quat_keys = 0;
    std::uint64_t pos_keys = 0;
    std::uint64_t time_keys = 0;
    std::uint64_t nonzero_flags = 0;
    std::int32_t quat_max = 0;
    std::int32_t pos_max = 0;
    std::int32_t time_max = 0;
};

bool ReadAnalogTrack(Cursor& cursor, TrackStats& stats, std::string& error) {
    std::uint32_t flags = 0;
    if (!cursor.U32(flags)) {
        error = "track_flags";
        return false;
    }
    stats.nonzero_flags += flags != 0u ? 1u : 0u;

    std::int32_t q_count = 0;
    if (!ReadCount(cursor, q_count)) {
        error = "quat_count";
        return false;
    }
    const std::uint64_t q_bytes = static_cast<std::uint64_t>(q_count) * 16u;
    if (q_bytes > cursor.Remaining() || !cursor.Skip(static_cast<std::size_t>(q_bytes))) {
        error = "quat_data";
        return false;
    }

    std::int32_t p_count = 0;
    if (!ReadCount(cursor, p_count)) {
        error = "pos_count";
        return false;
    }
    const std::uint64_t p_bytes = static_cast<std::uint64_t>(p_count) * 12u;
    if (p_bytes > cursor.Remaining() || !cursor.Skip(static_cast<std::size_t>(p_bytes))) {
        error = "pos_data";
        return false;
    }

    std::int32_t t_count = 0;
    if (!ReadCount(cursor, t_count)) {
        error = "time_count";
        return false;
    }
    const std::uint64_t t_bytes = static_cast<std::uint64_t>(t_count) * 4u;
    if (t_bytes > cursor.Remaining() || !cursor.Skip(static_cast<std::size_t>(t_bytes))) {
        error = "time_data";
        return false;
    }

    ++stats.tracks;
    stats.quat_keys += static_cast<std::uint64_t>(q_count);
    stats.pos_keys += static_cast<std::uint64_t>(p_count);
    stats.time_keys += static_cast<std::uint64_t>(t_count);
    stats.quat_max = std::max(stats.quat_max, q_count);
    stats.pos_max = std::max(stats.pos_max, p_count);
    stats.time_max = std::max(stats.time_max, t_count);
    return true;
}

struct MotionStats {
    bool valid = false;
    std::string error;
    std::size_t error_offset = 0;
    std::int32_t failed_move = -1;
    std::int32_t failed_track = -1;
    std::int32_t move_count = 0;
    std::int32_t completed_moves = 0;
    std::uint64_t bone_indices = 0;
    std::int32_t bone_indices_min = std::numeric_limits<std::int32_t>::max();
    std::int32_t bone_indices_max = 0;
    std::int32_t anim_tracks_min = std::numeric_limits<std::int32_t>::max();
    std::int32_t anim_tracks_max = 0;
    std::size_t moves_with_135_bone_indices = 0;
    std::size_t moves_with_135_tracks = 0;
    std::size_t root_tracks = 0;
    float track_time_min = std::numeric_limits<float>::infinity();
    float track_time_max = -std::numeric_limits<float>::infinity();
    std::int32_t start_bone_min = std::numeric_limits<std::int32_t>::max();
    std::int32_t start_bone_max = std::numeric_limits<std::int32_t>::min();
    std::uint64_t motion_flags_nonzero = 0;
    TrackStats tracks;
    std::size_t end_offset = 0;
    std::size_t remaining = 0;
};

MotionStats ParseMotions(const std::vector<std::uint8_t>& native, std::size_t start) {
    MotionStats result;
    Cursor cursor(native, start);
    if (!ReadCount(cursor, result.move_count, 4096)) {
        result.error = "move_count";
        result.error_offset = cursor.Position();
        return result;
    }

    for (std::int32_t move = 0; move < result.move_count; ++move) {
        result.failed_move = move;
        float root_x = 0.0f, root_y = 0.0f, root_z = 0.0f, track_time = 0.0f;
        std::int32_t start_bone = 0;
        std::uint32_t flags = 0;
        if (!cursor.F32(root_x) || !cursor.F32(root_y) || !cursor.F32(root_z)
            || !cursor.F32(track_time) || !cursor.I32(start_bone) || !cursor.U32(flags)) {
            result.error = "motion_header";
            result.error_offset = cursor.Position();
            return result;
        }
        if (!std::isfinite(root_x) || !std::isfinite(root_y) || !std::isfinite(root_z)
            || !std::isfinite(track_time)) {
            result.error = "motion_header_nonfinite";
            result.error_offset = cursor.Position();
            return result;
        }
        result.track_time_min = std::min(result.track_time_min, track_time);
        result.track_time_max = std::max(result.track_time_max, track_time);
        result.start_bone_min = std::min(result.start_bone_min, start_bone);
        result.start_bone_max = std::max(result.start_bone_max, start_bone);
        result.motion_flags_nonzero += flags != 0u ? 1u : 0u;

        std::int32_t bone_count = 0;
        if (!ReadCount(cursor, bone_count, 4096)) {
            result.error = "bone_indices_count";
            result.error_offset = cursor.Position();
            return result;
        }
        result.bone_indices += static_cast<std::uint64_t>(bone_count);
        result.bone_indices_min = std::min(result.bone_indices_min, bone_count);
        result.bone_indices_max = std::max(result.bone_indices_max, bone_count);
        result.moves_with_135_bone_indices += bone_count == 135 ? 1u : 0u;
        const std::uint64_t bone_bytes = static_cast<std::uint64_t>(bone_count) * 4u;
        if (bone_bytes > cursor.Remaining() || !cursor.Skip(static_cast<std::size_t>(bone_bytes))) {
            result.error = "bone_indices_data";
            result.error_offset = cursor.Position();
            return result;
        }

        std::int32_t track_count = 0;
        if (!ReadCount(cursor, track_count, 4096)) {
            result.error = "anim_tracks_count";
            result.error_offset = cursor.Position();
            return result;
        }
        result.anim_tracks_min = std::min(result.anim_tracks_min, track_count);
        result.anim_tracks_max = std::max(result.anim_tracks_max, track_count);
        result.moves_with_135_tracks += track_count == 135 ? 1u : 0u;
        for (std::int32_t track = 0; track < track_count; ++track) {
            result.failed_track = track;
            if (!ReadAnalogTrack(cursor, result.tracks, result.error)) {
                result.error_offset = cursor.Position();
                return result;
            }
        }

        result.failed_track = -1;
        if (!ReadAnalogTrack(cursor, result.tracks, result.error)) {
            result.error = "root_" + result.error;
            result.error_offset = cursor.Position();
            return result;
        }
        ++result.root_tracks;
        ++result.completed_moves;
    }

    result.failed_move = -1;
    result.failed_track = -1;
    result.valid = true;
    result.end_offset = cursor.Position();
    result.remaining = cursor.Remaining();
    if (result.bone_indices_min == std::numeric_limits<std::int32_t>::max()) result.bone_indices_min = 0;
    if (result.anim_tracks_min == std::numeric_limits<std::int32_t>::max()) result.anim_tracks_min = 0;
    if (result.start_bone_min == std::numeric_limits<std::int32_t>::max()) result.start_bone_min = 0;
    if (result.start_bone_max == std::numeric_limits<std::int32_t>::min()) result.start_bone_max = 0;
    if (!std::isfinite(result.track_time_min)) result.track_time_min = 0.0f;
    if (!std::isfinite(result.track_time_max)) result.track_time_max = 0.0f;
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: hp2_animation_motion_probe <game-root> <package> <animation-object>\n";
        return 64;
    }
    const auto package_path = FindPackage(argv[1], argv[2]);
    if (package_path.empty()) return 2;
    const hp2::PackageIndex package = hp2::LoadPackageIndex(package_path);
    if (!package.valid) return 2;
    const auto native = LoadAnimationNative(package, argv[3]);
    if (native.empty()) return 2;
    const std::size_t moves_offset = FindMovesOffset(native);
    if (moves_offset == 0u) return 2;
    const MotionStats stats = ParseMotions(native, moves_offset);

    std::int32_t next_compact = 0;
    std::size_t next_compact_bytes = 0;
    const bool next_compact_valid = stats.valid
        && DecodeCompact(native, stats.end_offset, next_compact, next_compact_bytes);

    std::cout << "{\n"
              << "  \"schema\": \"hp2-animation-motion-probe-v1\",\n"
              << "  \"native_bytes\": " << native.size() << ",\n"
              << "  \"moves_offset\": " << moves_offset << ",\n"
              << "  \"valid\": " << (stats.valid ? "true" : "false") << ",\n"
              << "  \"error\": \"" << stats.error << "\",\n"
              << "  \"error_offset\": " << stats.error_offset << ",\n"
              << "  \"failed_move\": " << stats.failed_move << ",\n"
              << "  \"failed_track\": " << stats.failed_track << ",\n"
              << "  \"move_count\": " << stats.move_count << ",\n"
              << "  \"completed_moves\": " << stats.completed_moves << ",\n"
              << "  \"bone_indices_total\": " << stats.bone_indices << ",\n"
              << "  \"bone_indices_min\": " << stats.bone_indices_min << ",\n"
              << "  \"bone_indices_max\": " << stats.bone_indices_max << ",\n"
              << "  \"moves_with_135_bone_indices\": " << stats.moves_with_135_bone_indices << ",\n"
              << "  \"anim_tracks_min\": " << stats.anim_tracks_min << ",\n"
              << "  \"anim_tracks_max\": " << stats.anim_tracks_max << ",\n"
              << "  \"moves_with_135_tracks\": " << stats.moves_with_135_tracks << ",\n"
              << "  \"root_tracks\": " << stats.root_tracks << ",\n"
              << "  \"track_time_min\": " << stats.track_time_min << ",\n"
              << "  \"track_time_max\": " << stats.track_time_max << ",\n"
              << "  \"start_bone_min\": " << stats.start_bone_min << ",\n"
              << "  \"start_bone_max\": " << stats.start_bone_max << ",\n"
              << "  \"motion_flags_nonzero\": " << stats.motion_flags_nonzero << ",\n"
              << "  \"analog_tracks_total_including_roots\": " << stats.tracks.tracks << ",\n"
              << "  \"analog_track_flags_nonzero\": " << stats.tracks.nonzero_flags << ",\n"
              << "  \"quat_keys\": " << stats.tracks.quat_keys << ",\n"
              << "  \"pos_keys\": " << stats.tracks.pos_keys << ",\n"
              << "  \"time_keys\": " << stats.tracks.time_keys << ",\n"
              << "  \"quat_keys_max_per_track\": " << stats.tracks.quat_max << ",\n"
              << "  \"pos_keys_max_per_track\": " << stats.tracks.pos_max << ",\n"
              << "  \"time_keys_max_per_track\": " << stats.tracks.time_max << ",\n"
              << "  \"end_offset\": " << stats.end_offset << ",\n"
              << "  \"bytes_remaining\": " << stats.remaining << ",\n"
              << "  \"next_compact_valid\": " << (next_compact_valid ? "true" : "false") << ",\n"
              << "  \"next_compact_value\": " << (next_compact_valid ? next_compact : 0) << ",\n"
              << "  \"next_compact_bytes\": " << (next_compact_valid ? next_compact_bytes : 0u) << "\n"
              << "}\n";
    return stats.valid ? 0 : 3;
}
