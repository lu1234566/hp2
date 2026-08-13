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

std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::filesystem::path FindPackage(const std::filesystem::path& root, const std::string& stem) {
    const std::string wanted = Lowercase(stem);
    std::error_code ec;
    const auto opts = std::filesystem::directory_options::skip_permission_denied;
    for (std::filesystem::recursive_directory_iterator it(root, opts, ec), end; it != end;) {
        if (ec) {
            ec.clear();
            it.increment(ec);
            continue;
        }
        if (it->is_regular_file(ec) && !ec && hp2::IsPackageExtension(it->path())
            && Lowercase(it->path().stem().string()) == wanted) {
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
    if (magnitude > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
        return false;
    }
    const auto signed_value = static_cast<std::int32_t>(magnitude);
    value = negative ? -signed_value : signed_value;
    return true;
}

class Cursor {
public:
    Cursor(const std::vector<std::uint8_t>& bytes, std::size_t position)
        : bytes_(bytes), position_(position) {}

    std::size_t Position() const { return position_; }
    std::size_t Remaining() const {
        return position_ <= bytes_.size() ? bytes_.size() - position_ : 0u;
    }

    bool Compact(std::int32_t& value) {
        std::size_t used = 0;
        if (!DecodeCompact(bytes_, position_, value, used)) return false;
        position_ += used;
        return true;
    }

    bool Skip(std::size_t count) {
        if (count > Remaining()) return false;
        position_ += count;
        return true;
    }

private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t position_;
};

bool ReadCount(Cursor& cursor, std::int32_t& count) {
    return cursor.Compact(count) && count >= 0 && count <= kMaxCount;
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

struct ArraySpec {
    char kind = '?';
    std::size_t stride = 0;
};

struct Layout {
    const char* name = "";
    bool leading_flags = true;
    std::vector<ArraySpec> arrays;
};

struct LayoutResult {
    std::string name;
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
    std::uint64_t q_keys = 0;
    std::uint64_t p_keys = 0;
    std::uint64_t t_keys = 0;
    std::int32_t track_count_min = std::numeric_limits<std::int32_t>::max();
    std::int32_t track_count_max = 0;
    std::size_t moves_with_135_tracks = 0;
};

LayoutResult Evaluate(
    const std::vector<std::uint8_t>& native,
    std::size_t motion_start,
    const Layout& layout
) {
    LayoutResult result;
    result.name = layout.name;
    Cursor cursor(native, motion_start);
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
        result.track_count_min = std::min(result.track_count_min, track_count);
        result.track_count_max = std::max(result.track_count_max, track_count);
        result.moves_with_135_tracks += track_count == 135 ? 1u : 0u;
        for (std::int32_t track = 0; track < track_count; ++track) {
            result.failed_track = track;
            if (layout.leading_flags && !cursor.Skip(4u)) {
                result.error_stage = "track_flags";
                result.failure_offset = cursor.Position();
                return result;
            }
            for (const ArraySpec& array : layout.arrays) {
                std::int32_t count = 0;
                if (!ReadCount(cursor, count)) {
                    result.error_stage = std::string("count_") + array.kind;
                    result.failure_offset = cursor.Position();
                    return result;
                }
                const std::uint64_t bytes = static_cast<std::uint64_t>(count) * array.stride;
                if (bytes > cursor.Remaining()
                    || !cursor.Skip(static_cast<std::size_t>(bytes))) {
                    result.error_stage = std::string("keys_") + array.kind;
                    result.failure_offset = cursor.Position();
                    return result;
                }
                if (array.kind == 'q') result.q_keys += static_cast<std::uint64_t>(count);
                if (array.kind == 'p') result.p_keys += static_cast<std::uint64_t>(count);
                if (array.kind == 't') result.t_keys += static_cast<std::uint64_t>(count);
            }
            ++result.completed_tracks;
        }
        ++result.completed_moves;
    }
    if (result.track_count_min == std::numeric_limits<std::int32_t>::max()) {
        result.track_count_min = 0;
    }
    result.failed_move = -1;
    result.failed_track = -1;
    result.end_offset = cursor.Position();
    result.bytes_remaining = cursor.Remaining();
    result.valid = true;
    return result;
}

std::vector<std::uint8_t> LoadAnimationNative(
    const hp2::PackageIndex& package,
    const std::string& object_name
) {
    for (std::size_t index = 0; index < package.exports.size(); ++index) {
        const auto& entry = package.exports[index];
        if (Lowercase(entry.class_name) != "animation"
            || Lowercase(entry.object_name) != Lowercase(object_name)) {
            continue;
        }
        const hp2::ObjectProperties properties = hp2::LoadObjectProperties(package, index);
        if (!properties.valid || entry.serial_size <= 0 || entry.serial_offset < 0) return {};
        std::ifstream input(package.summary.path, std::ios::binary);
        input.seekg(entry.serial_offset, std::ios::beg);
        std::vector<std::uint8_t> payload(static_cast<std::size_t>(entry.serial_size));
        input.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
        if (input.gcount() != static_cast<std::streamsize>(payload.size())
            || properties.native_data_offset > payload.size()) {
            return {};
        }
        return std::vector<std::uint8_t>(
            payload.begin() + static_cast<std::ptrdiff_t>(properties.native_data_offset),
            payload.end()
        );
    }
    return {};
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: hp2_animation_layout_probe <game-root> <package> <animation-object>\n";
        return 64;
    }
    const auto package_path = FindPackage(argv[1], argv[2]);
    if (package_path.empty()) return 2;
    const hp2::PackageIndex package = hp2::LoadPackageIndex(package_path);
    if (!package.valid) return 2;
    const std::vector<std::uint8_t> native = LoadAnimationNative(package, argv[3]);
    if (native.empty()) return 2;
    const std::size_t motion_start = FindMotionStart(native);
    if (motion_start == 0u) return 2;

    const std::vector<Layout> layouts = {
        {"flags_q16_p12_t4", true, {{'q', 16u}, {'p', 12u}, {'t', 4u}}},
        {"flags_q20_p12_t4", true, {{'q', 20u}, {'p', 12u}, {'t', 4u}}},
        {"flags_q16_p16_t4", true, {{'q', 16u}, {'p', 16u}, {'t', 4u}}},
        {"flags_q20_p16_t4", true, {{'q', 20u}, {'p', 16u}, {'t', 4u}}},
        {"flags_q16_p12", true, {{'q', 16u}, {'p', 12u}}},
        {"flags_q20_p12", true, {{'q', 20u}, {'p', 12u}}},
        {"flags_q16_p16", true, {{'q', 16u}, {'p', 16u}}},
        {"flags_q20_p16", true, {{'q', 20u}, {'p', 16u}}},
        {"flags_p12_q16_t4", true, {{'p', 12u}, {'q', 16u}, {'t', 4u}}},
        {"flags_p12_q20_t4", true, {{'p', 12u}, {'q', 20u}, {'t', 4u}}},
        {"flags_q16_t4_p12", true, {{'q', 16u}, {'t', 4u}, {'p', 12u}}},
        {"flags_q20_t4_p12", true, {{'q', 20u}, {'t', 4u}, {'p', 12u}}},
        {"q16_p12_t4", false, {{'q', 16u}, {'p', 12u}, {'t', 4u}}},
        {"q20_p12_t4", false, {{'q', 20u}, {'p', 12u}, {'t', 4u}}},
    };

    std::vector<LayoutResult> results;
    for (const auto& layout : layouts) {
        results.push_back(Evaluate(native, motion_start, layout));
    }
    std::sort(results.begin(), results.end(), [](const LayoutResult& a, const LayoutResult& b) {
        if (a.valid != b.valid) return a.valid > b.valid;
        if (a.completed_moves != b.completed_moves) return a.completed_moves > b.completed_moves;
        if (a.completed_tracks != b.completed_tracks) return a.completed_tracks > b.completed_tracks;
        return a.failure_offset > b.failure_offset;
    });

    std::cout << "{\n"
              << "  \"schema\": \"hp2-animation-layout-probe-v1\",\n"
              << "  \"native_bytes\": " << native.size() << ",\n"
              << "  \"motion_start\": " << motion_start << ",\n"
              << "  \"layouts\": [\n";
    for (std::size_t index = 0; index < results.size(); ++index) {
        const auto& r = results[index];
        std::cout << "    {\"name\": \"" << r.name
                  << "\", \"valid\": " << (r.valid ? "true" : "false")
                  << ", \"move_count\": " << r.move_count
                  << ", \"completed_moves\": " << r.completed_moves
                  << ", \"completed_tracks\": " << r.completed_tracks
                  << ", \"failed_move\": " << r.failed_move
                  << ", \"failed_track\": " << r.failed_track
                  << ", \"error_stage\": \"" << r.error_stage
                  << "\", \"failure_offset\": " << r.failure_offset
                  << ", \"end_offset\": " << r.end_offset
                  << ", \"bytes_remaining\": " << r.bytes_remaining
                  << ", \"track_count_min\": " << r.track_count_min
                  << ", \"track_count_max\": " << r.track_count_max
                  << ", \"moves_with_135_tracks\": " << r.moves_with_135_tracks
                  << ", \"q_keys\": " << r.q_keys
                  << ", \"p_keys\": " << r.p_keys
                  << ", \"t_keys\": " << r.t_keys << "}"
                  << (index + 1u == results.size() ? "" : ",") << '\n';
    }
    std::cout << "  ]\n}\n";
    return 0;
}
