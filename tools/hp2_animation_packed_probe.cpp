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
    Cursor(const std::vector<std::uint8_t>& bytes, std::size_t position)
        : bytes_(bytes), position_(position) {}

    std::size_t Position() const { return position_; }
    std::size_t Remaining() const { return position_ <= bytes_.size() ? bytes_.size() - position_ : 0u; }

    bool Compact(std::int32_t& value) {
        std::size_t used = 0;
        if (!DecodeCompact(bytes_, position_, value, used)) return false;
        position_ += used;
        return true;
    }

    bool Skip(std::size_t bytes) {
        if (bytes > Remaining()) return false;
        position_ += bytes;
        return true;
    }

private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t position_ = 0;
};

bool ReadCount(Cursor& cursor, std::int32_t& count) {
    return cursor.Compact(count) && count >= 0 && count <= kMaxCount;
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
            payload.begin() + static_cast<std::ptrdiff_t>(properties.native_data_offset),
            payload.end()
        );
    }
    return {};
}

std::size_t FindMotionStart(const std::vector<std::uint8_t>& native) {
    Cursor cursor(native, 0u);
    std::int32_t bone_count = 0;
    if (!ReadCount(cursor, bone_count) || bone_count > 4096) return 0u;
    for (std::int32_t index = 0; index < bone_count; ++index) {
        std::int32_t name_index = 0;
        if (!cursor.Compact(name_index) || !cursor.Skip(8u)) return 0u;
    }
    return cursor.Position();
}

struct LayoutResult {
    std::size_t quat_stride = 0;
    std::size_t pos_stride = 0;
    std::size_t time_stride = 0;
    bool valid = false;
    std::string error_stage;
    std::int32_t move_count = 0;
    std::int32_t completed_moves = 0;
    std::uint64_t completed_tracks = 0;
    std::int32_t failed_move = -1;
    std::int32_t failed_track = -1;
    std::size_t failure_offset = 0;
    std::size_t end_offset = 0;
    std::size_t bytes_remaining = 0;
    std::uint64_t quat_keys = 0;
    std::uint64_t pos_keys = 0;
    std::uint64_t time_keys = 0;
    std::int32_t track_min = std::numeric_limits<std::int32_t>::max();
    std::int32_t track_max = 0;
    std::size_t moves_with_135_tracks = 0;
};

LayoutResult Evaluate(
    const std::vector<std::uint8_t>& native,
    std::size_t start,
    std::size_t quat_stride,
    std::size_t pos_stride,
    std::size_t time_stride
) {
    LayoutResult result;
    result.quat_stride = quat_stride;
    result.pos_stride = pos_stride;
    result.time_stride = time_stride;
    Cursor cursor(native, start);
    if (!ReadCount(cursor, result.move_count) || result.move_count > 4096) {
        result.error_stage = "move_count";
        result.failure_offset = cursor.Position();
        return result;
    }

    for (std::int32_t move = 0; move < result.move_count; ++move) {
        result.failed_move = move;
        if (!cursor.Skip(24u)) {
            result.error_stage = "motion_header";
            result.failure_offset = cursor.Position();
            return result;
        }
        std::int32_t track_count = 0;
        if (!ReadCount(cursor, track_count) || track_count > 4096) {
            result.error_stage = "track_count";
            result.failure_offset = cursor.Position();
            return result;
        }
        result.track_min = std::min(result.track_min, track_count);
        result.track_max = std::max(result.track_max, track_count);
        result.moves_with_135_tracks += track_count == 135 ? 1u : 0u;

        for (std::int32_t track = 0; track < track_count; ++track) {
            result.failed_track = track;
            if (!cursor.Skip(4u)) {
                result.error_stage = "flags";
                result.failure_offset = cursor.Position();
                return result;
            }

            std::int32_t q_count = 0;
            if (!ReadCount(cursor, q_count)) {
                result.error_stage = "quat_count";
                result.failure_offset = cursor.Position();
                return result;
            }
            const std::uint64_t q_bytes = static_cast<std::uint64_t>(q_count) * quat_stride;
            if (q_bytes > cursor.Remaining() || !cursor.Skip(static_cast<std::size_t>(q_bytes))) {
                result.error_stage = "quat_data";
                result.failure_offset = cursor.Position();
                return result;
            }
            result.quat_keys += static_cast<std::uint64_t>(q_count);

            std::int32_t p_count = 0;
            if (!ReadCount(cursor, p_count)) {
                result.error_stage = "pos_count";
                result.failure_offset = cursor.Position();
                return result;
            }
            const std::uint64_t p_bytes = static_cast<std::uint64_t>(p_count) * pos_stride;
            if (p_bytes > cursor.Remaining() || !cursor.Skip(static_cast<std::size_t>(p_bytes))) {
                result.error_stage = "pos_data";
                result.failure_offset = cursor.Position();
                return result;
            }
            result.pos_keys += static_cast<std::uint64_t>(p_count);

            std::int32_t t_count = 0;
            if (!ReadCount(cursor, t_count)) {
                result.error_stage = "time_count";
                result.failure_offset = cursor.Position();
                return result;
            }
            const std::uint64_t t_bytes = static_cast<std::uint64_t>(t_count) * time_stride;
            if (t_bytes > cursor.Remaining() || !cursor.Skip(static_cast<std::size_t>(t_bytes))) {
                result.error_stage = "time_data";
                result.failure_offset = cursor.Position();
                return result;
            }
            result.time_keys += static_cast<std::uint64_t>(t_count);
            ++result.completed_tracks;
        }
        ++result.completed_moves;
    }

    if (result.track_min == std::numeric_limits<std::int32_t>::max()) result.track_min = 0;
    result.failed_move = -1;
    result.failed_track = -1;
    result.valid = true;
    result.end_offset = cursor.Position();
    result.bytes_remaining = cursor.Remaining();
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: hp2_animation_packed_probe <game-root> <package> <animation-object>\n";
        return 64;
    }
    const auto package_path = FindPackage(argv[1], argv[2]);
    if (package_path.empty()) return 2;
    const hp2::PackageIndex package = hp2::LoadPackageIndex(package_path);
    if (!package.valid) return 2;
    const auto native = LoadAnimationNative(package, argv[3]);
    if (native.empty()) return 2;
    const std::size_t motion_start = FindMotionStart(native);
    if (motion_start == 0u) return 2;

    const std::vector<std::size_t> quat_strides = {6u, 8u, 10u, 12u, 16u};
    const std::vector<std::size_t> pos_strides = {6u, 8u, 12u, 16u};
    const std::vector<std::size_t> time_strides = {1u, 2u, 4u};
    std::vector<LayoutResult> results;
    for (const auto q : quat_strides) {
        for (const auto p : pos_strides) {
            for (const auto t : time_strides) {
                results.push_back(Evaluate(native, motion_start, q, p, t));
            }
        }
    }
    std::sort(results.begin(), results.end(), [](const LayoutResult& a, const LayoutResult& b) {
        if (a.valid != b.valid) return a.valid > b.valid;
        if (a.completed_moves != b.completed_moves) return a.completed_moves > b.completed_moves;
        if (a.completed_tracks != b.completed_tracks) return a.completed_tracks > b.completed_tracks;
        return a.failure_offset > b.failure_offset;
    });

    const std::size_t emit_count = std::min<std::size_t>(20u, results.size());
    std::cout << "{\n"
              << "  \"schema\": \"hp2-animation-packed-probe-v1\",\n"
              << "  \"native_bytes\": " << native.size() << ",\n"
              << "  \"motion_start\": " << motion_start << ",\n"
              << "  \"candidate_count\": " << results.size() << ",\n"
              << "  \"top_candidates\": [\n";
    for (std::size_t index = 0; index < emit_count; ++index) {
        const auto& r = results[index];
        std::cout << "    {\"q\":" << r.quat_stride
                  << ",\"p\":" << r.pos_stride
                  << ",\"t\":" << r.time_stride
                  << ",\"valid\":" << (r.valid ? "true" : "false")
                  << ",\"moves\":" << r.move_count
                  << ",\"complete_moves\":" << r.completed_moves
                  << ",\"complete_tracks\":" << r.completed_tracks
                  << ",\"failed_move\":" << r.failed_move
                  << ",\"failed_track\":" << r.failed_track
                  << ",\"error\":\"" << r.error_stage
                  << "\",\"offset\":" << r.failure_offset
                  << ",\"end\":" << r.end_offset
                  << ",\"remaining\":" << r.bytes_remaining
                  << ",\"track_min\":" << r.track_min
                  << ",\"track_max\":" << r.track_max
                  << ",\"moves_135\":" << r.moves_with_135_tracks
                  << ",\"q_keys\":" << r.quat_keys
                  << ",\"p_keys\":" << r.pos_keys
                  << ",\"t_keys\":" << r.time_keys << "}"
                  << (index + 1u == emit_count ? "" : ",") << '\n';
    }
    std::cout << "  ]\n}\n";
    return 0;
}
