#include "hp2/ue_actor.h"
#include "hp2/ue_package.h"
#include "hp2/ue_skeletal.h"

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

constexpr std::int32_t kMaxArrayCount = 2'000'000;

std::string Lowercase(std::string value) {
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
            default:
                if (static_cast<unsigned char>(c) >= 0x20u) out += c;
                break;
        }
    }
    return out;
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
    explicit Cursor(const std::vector<std::uint8_t>& bytes, std::size_t position = 0)
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

    bool Skip(std::size_t bytes) {
        if (bytes > Remaining()) return false;
        position_ += bytes;
        return true;
    }

private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t position_ = 0;
};

struct BoneChannel {
    std::string name;
    std::uint32_t flags = 0;
    std::int32_t parent = -1;
};

struct BoneChannelTable {
    bool valid = false;
    std::int32_t count = 0;
    std::size_t end_offset = 0;
    std::size_t valid_name_count = 0;
    std::size_t flags_nonzero = 0;
    std::size_t parent_zero = 0;
    std::int32_t parent_max = -1;
    std::vector<BoneChannel> channels;
};

BoneChannelTable ParseBoneChannelTable(
    const hp2::PackageIndex& package,
    const std::vector<std::uint8_t>& native
) {
    BoneChannelTable result;
    Cursor cursor(native);
    if (!cursor.Compact(result.count) || result.count < 0 || result.count > 4096) {
        return result;
    }
    result.channels.reserve(static_cast<std::size_t>(result.count));
    for (std::int32_t index = 0; index < result.count; ++index) {
        std::int32_t name_index = -1;
        std::uint32_t flags = 0;
        std::int32_t parent = -1;
        if (!cursor.Compact(name_index) || !cursor.U32(flags) || !cursor.I32(parent)) {
            return result;
        }
        BoneChannel channel;
        channel.flags = flags;
        channel.parent = parent;
        if (name_index >= 0 && static_cast<std::size_t>(name_index) < package.names.size()) {
            channel.name = package.names[static_cast<std::size_t>(name_index)].value;
            ++result.valid_name_count;
        }
        result.flags_nonzero += flags != 0u ? 1u : 0u;
        result.parent_zero += parent == 0 ? 1u : 0u;
        result.parent_max = std::max(result.parent_max, parent);
        result.channels.push_back(std::move(channel));
    }
    result.end_offset = cursor.Position();
    result.valid = result.valid_name_count == static_cast<std::size_t>(result.count);
    return result;
}

struct SkeletonComparison {
    bool available = false;
    std::size_t mesh_bones = 0;
    std::size_t name_matches = 0;
    std::size_t parent_matches = 0;
    std::size_t full_matches = 0;
};

SkeletonComparison CompareWithMeshSkeleton(
    const std::filesystem::path& root,
    const BoneChannelTable& table
) {
    SkeletonComparison result;
    const hp2::SkeletalMeshSkinningData mesh = hp2::LoadNamedSkeletalMeshSkinning(
        root, "HPModels", "skhp2_genmale1Mesh"
    );
    if (!mesh.valid) return result;
    result.available = true;
    result.mesh_bones = mesh.bones.size();
    const std::size_t count = std::min(table.channels.size(), mesh.bones.size());
    for (std::size_t index = 0; index < count; ++index) {
        const bool name_match = Lowercase(table.channels[index].name)
            == Lowercase(mesh.bones[index].name);
        const bool parent_match = table.channels[index].parent == mesh.bones[index].parent_index;
        result.name_matches += name_match ? 1u : 0u;
        result.parent_matches += parent_match ? 1u : 0u;
        result.full_matches += (name_match && parent_match) ? 1u : 0u;
    }
    return result;
}

struct MotionStats {
    bool valid = false;
    std::string error_stage;
    std::size_t error_offset = 0;
    std::int32_t error_move = -1;
    std::int32_t error_track = -1;
    std::int32_t move_count = 0;
    std::size_t end_offset = 0;
    std::size_t total_tracks = 0;
    std::int32_t track_count_min = std::numeric_limits<std::int32_t>::max();
    std::int32_t track_count_max = 0;
    std::size_t moves_with_135_tracks = 0;
    std::size_t nonfinite_header_floats = 0;
    std::int32_t start_bone_min = std::numeric_limits<std::int32_t>::max();
    std::int32_t start_bone_max = std::numeric_limits<std::int32_t>::min();
    std::size_t motion_flags_nonzero = 0;
    std::size_t track_flags_nonzero = 0;
    std::uint64_t quaternion_keys = 0;
    std::uint64_t position_keys = 0;
    std::uint64_t time_keys = 0;
    std::int32_t quaternion_keys_max_per_track = 0;
    std::int32_t position_keys_max_per_track = 0;
    std::int32_t time_keys_max_per_track = 0;
    float track_time_min = std::numeric_limits<float>::infinity();
    float track_time_max = -std::numeric_limits<float>::infinity();
};

bool ReadBoundedCount(Cursor& cursor, std::int32_t& count) {
    return cursor.Compact(count) && count >= 0 && count <= kMaxArrayCount;
}

MotionStats ParseMotionChunks(
    const std::vector<std::uint8_t>& native,
    std::size_t start_offset
) {
    MotionStats result;
    Cursor cursor(native, start_offset);
    if (!ReadBoundedCount(cursor, result.move_count) || result.move_count > 4096) {
        result.error_stage = "move_count";
        result.error_offset = cursor.Position();
        return result;
    }

    for (std::int32_t move_index = 0; move_index < result.move_count; ++move_index) {
        result.error_move = move_index;
        float root_x = 0.0f;
        float root_y = 0.0f;
        float root_z = 0.0f;
        float track_time = 0.0f;
        std::int32_t start_bone = 0;
        std::uint32_t flags = 0;
        if (!cursor.F32(root_x) || !cursor.F32(root_y) || !cursor.F32(root_z)
            || !cursor.F32(track_time) || !cursor.I32(start_bone) || !cursor.U32(flags)) {
            result.error_stage = "motion_header";
            result.error_offset = cursor.Position();
            return result;
        }
        result.nonfinite_header_floats +=
            (!std::isfinite(root_x) ? 1u : 0u)
            + (!std::isfinite(root_y) ? 1u : 0u)
            + (!std::isfinite(root_z) ? 1u : 0u)
            + (!std::isfinite(track_time) ? 1u : 0u);
        if (std::isfinite(track_time)) {
            result.track_time_min = std::min(result.track_time_min, track_time);
            result.track_time_max = std::max(result.track_time_max, track_time);
        }
        result.start_bone_min = std::min(result.start_bone_min, start_bone);
        result.start_bone_max = std::max(result.start_bone_max, start_bone);
        result.motion_flags_nonzero += flags != 0u ? 1u : 0u;

        std::int32_t track_count = 0;
        if (!ReadBoundedCount(cursor, track_count) || track_count > 4096) {
            result.error_stage = "track_count";
            result.error_offset = cursor.Position();
            return result;
        }
        result.total_tracks += static_cast<std::size_t>(track_count);
        result.track_count_min = std::min(result.track_count_min, track_count);
        result.track_count_max = std::max(result.track_count_max, track_count);
        result.moves_with_135_tracks += track_count == 135 ? 1u : 0u;

        for (std::int32_t track_index = 0; track_index < track_count; ++track_index) {
            result.error_track = track_index;
            std::uint32_t track_flags = 0;
            if (!cursor.U32(track_flags)) {
                result.error_stage = "track_flags";
                result.error_offset = cursor.Position();
                return result;
            }
            result.track_flags_nonzero += track_flags != 0u ? 1u : 0u;

            std::int32_t quaternion_count = 0;
            if (!ReadBoundedCount(cursor, quaternion_count)) {
                result.error_stage = "quaternion_count";
                result.error_offset = cursor.Position();
                return result;
            }
            const std::uint64_t quaternion_bytes = static_cast<std::uint64_t>(quaternion_count) * 16u;
            if (quaternion_bytes > cursor.Remaining()
                || !cursor.Skip(static_cast<std::size_t>(quaternion_bytes))) {
                result.error_stage = "quaternion_keys";
                result.error_offset = cursor.Position();
                return result;
            }
            result.quaternion_keys += static_cast<std::uint64_t>(quaternion_count);
            result.quaternion_keys_max_per_track = std::max(
                result.quaternion_keys_max_per_track, quaternion_count
            );

            std::int32_t position_count = 0;
            if (!ReadBoundedCount(cursor, position_count)) {
                result.error_stage = "position_count";
                result.error_offset = cursor.Position();
                return result;
            }
            const std::uint64_t position_bytes = static_cast<std::uint64_t>(position_count) * 12u;
            if (position_bytes > cursor.Remaining()
                || !cursor.Skip(static_cast<std::size_t>(position_bytes))) {
                result.error_stage = "position_keys";
                result.error_offset = cursor.Position();
                return result;
            }
            result.position_keys += static_cast<std::uint64_t>(position_count);
            result.position_keys_max_per_track = std::max(
                result.position_keys_max_per_track, position_count
            );

            std::int32_t time_count = 0;
            if (!ReadBoundedCount(cursor, time_count)) {
                result.error_stage = "time_count";
                result.error_offset = cursor.Position();
                return result;
            }
            const std::uint64_t time_bytes = static_cast<std::uint64_t>(time_count) * 4u;
            if (time_bytes > cursor.Remaining()
                || !cursor.Skip(static_cast<std::size_t>(time_bytes))) {
                result.error_stage = "time_keys";
                result.error_offset = cursor.Position();
                return result;
            }
            result.time_keys += static_cast<std::uint64_t>(time_count);
            result.time_keys_max_per_track = std::max(result.time_keys_max_per_track, time_count);
        }
    }
    if (result.move_count == 0) {
        result.track_count_min = 0;
        result.start_bone_min = 0;
        result.start_bone_max = 0;
        result.track_time_min = 0.0f;
        result.track_time_max = 0.0f;
    }
    result.error_move = -1;
    result.error_track = -1;
    result.end_offset = cursor.Position();
    result.valid = true;
    return result;
}

void PrintNameSample(const std::vector<BoneChannel>& channels, bool first) {
    const std::size_t sample = std::min<std::size_t>(10u, channels.size());
    std::cout << '[';
    for (std::size_t index = 0; index < sample; ++index) {
        const std::size_t source_index = first ? index : channels.size() - sample + index;
        if (index != 0u) std::cout << ", ";
        std::cout << '"' << JsonEscape(channels[source_index].name) << '"';
    }
    std::cout << ']';
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: hp2_animation_probe <game-root> <package> <animation-object>\n";
        return 64;
    }

    const std::filesystem::path root = argv[1];
    const std::filesystem::path package_path = FindPackage(root, argv[2]);
    if (package_path.empty()) {
        std::cerr << "Package not found\n";
        return 2;
    }
    const hp2::PackageIndex package = hp2::LoadPackageIndex(package_path);
    if (!package.valid) {
        std::cerr << package.error << '\n';
        return 2;
    }

    std::size_t export_index = std::numeric_limits<std::size_t>::max();
    for (std::size_t index = 0; index < package.exports.size(); ++index) {
        const auto& entry = package.exports[index];
        if (Lowercase(entry.class_name) == "animation"
            && Lowercase(entry.object_name) == Lowercase(argv[3])) {
            export_index = index;
            break;
        }
    }
    if (export_index == std::numeric_limits<std::size_t>::max()) {
        std::cerr << "Animation export not found\n";
        return 2;
    }

    const auto& entry = package.exports[export_index];
    const hp2::ObjectProperties properties = hp2::LoadObjectProperties(package, export_index);
    if (!properties.valid) {
        std::cerr << properties.error << '\n';
        return 2;
    }

    std::ifstream input(package_path, std::ios::binary);
    input.seekg(entry.serial_offset, std::ios::beg);
    std::vector<std::uint8_t> payload(static_cast<std::size_t>(entry.serial_size));
    input.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    if (input.gcount() != static_cast<std::streamsize>(payload.size())) {
        std::cerr << "Short Animation payload read\n";
        return 2;
    }
    if (properties.native_data_offset > payload.size()) {
        std::cerr << "Native offset is outside Animation payload\n";
        return 2;
    }
    const std::vector<std::uint8_t> native(
        payload.begin() + static_cast<std::ptrdiff_t>(properties.native_data_offset), payload.end()
    );

    const BoneChannelTable bone_table = ParseBoneChannelTable(package, native);
    const SkeletonComparison comparison = CompareWithMeshSkeleton(root, bone_table);
    const MotionStats motions = bone_table.valid
        ? ParseMotionChunks(native, bone_table.end_offset)
        : MotionStats{};

    std::int32_t next_compact = 0;
    std::size_t next_compact_bytes = 0;
    const bool next_compact_valid = motions.valid
        && DecodeCompact(native, motions.end_offset, next_compact, next_compact_bytes);

    std::cout << "{\n"
              << "  \"schema\": \"hp2-animation-probe-v3\",\n"
              << "  \"package\": \"" << JsonEscape(package.summary.path.stem().string()) << "\",\n"
              << "  \"object\": \"" << JsonEscape(entry.object_name) << "\",\n"
              << "  \"class\": \"" << JsonEscape(entry.class_name) << "\",\n"
              << "  \"version\": " << package.summary.file_version << ",\n"
              << "  \"serial_size\": " << entry.serial_size << ",\n"
              << "  \"native_offset\": " << properties.native_data_offset << ",\n"
              << "  \"native_bytes\": " << native.size() << ",\n"
              << "  \"bone_table_valid\": " << (bone_table.valid ? "true" : "false") << ",\n"
              << "  \"bone_channel_count\": " << bone_table.count << ",\n"
              << "  \"bone_table_end_offset\": " << bone_table.end_offset << ",\n"
              << "  \"bone_flags_nonzero\": " << bone_table.flags_nonzero << ",\n"
              << "  \"bone_parent_zero\": " << bone_table.parent_zero << ",\n"
              << "  \"bone_parent_max\": " << bone_table.parent_max << ",\n"
              << "  \"bone_first_names\": ";
    PrintNameSample(bone_table.channels, true);
    std::cout << ",\n  \"bone_last_names\": ";
    PrintNameSample(bone_table.channels, false);
    std::cout << ",\n"
              << "  \"mesh_skeleton_available\": " << (comparison.available ? "true" : "false") << ",\n"
              << "  \"mesh_bones\": " << comparison.mesh_bones << ",\n"
              << "  \"mesh_name_matches\": " << comparison.name_matches << ",\n"
              << "  \"mesh_parent_matches\": " << comparison.parent_matches << ",\n"
              << "  \"mesh_full_matches\": " << comparison.full_matches << ",\n"
              << "  \"motions_valid\": " << (motions.valid ? "true" : "false") << ",\n"
              << "  \"motion_count\": " << motions.move_count << ",\n"
              << "  \"motion_error_stage\": \"" << JsonEscape(motions.error_stage) << "\",\n"
              << "  \"motion_error_offset\": " << motions.error_offset << ",\n"
              << "  \"motion_error_move\": " << motions.error_move << ",\n"
              << "  \"motion_error_track\": " << motions.error_track << ",\n"
              << "  \"motion_end_offset\": " << motions.end_offset << ",\n"
              << "  \"total_tracks\": " << motions.total_tracks << ",\n"
              << "  \"track_count_min\": "
              << (motions.track_count_min == std::numeric_limits<std::int32_t>::max() ? 0 : motions.track_count_min) << ",\n"
              << "  \"track_count_max\": " << motions.track_count_max << ",\n"
              << "  \"moves_with_135_tracks\": " << motions.moves_with_135_tracks << ",\n"
              << "  \"nonfinite_header_floats\": " << motions.nonfinite_header_floats << ",\n"
              << "  \"start_bone_min\": "
              << (motions.start_bone_min == std::numeric_limits<std::int32_t>::max() ? 0 : motions.start_bone_min) << ",\n"
              << "  \"start_bone_max\": "
              << (motions.start_bone_max == std::numeric_limits<std::int32_t>::min() ? 0 : motions.start_bone_max) << ",\n"
              << "  \"motion_flags_nonzero\": " << motions.motion_flags_nonzero << ",\n"
              << "  \"track_flags_nonzero\": " << motions.track_flags_nonzero << ",\n"
              << "  \"quaternion_keys\": " << motions.quaternion_keys << ",\n"
              << "  \"position_keys\": " << motions.position_keys << ",\n"
              << "  \"time_keys\": " << motions.time_keys << ",\n"
              << "  \"quaternion_keys_max_per_track\": " << motions.quaternion_keys_max_per_track << ",\n"
              << "  \"position_keys_max_per_track\": " << motions.position_keys_max_per_track << ",\n"
              << "  \"time_keys_max_per_track\": " << motions.time_keys_max_per_track << ",\n"
              << "  \"track_time_min\": " << (std::isfinite(motions.track_time_min) ? motions.track_time_min : 0.0f) << ",\n"
              << "  \"track_time_max\": " << (std::isfinite(motions.track_time_max) ? motions.track_time_max : 0.0f) << ",\n"
              << "  \"bytes_after_motions\": "
              << (motions.valid && motions.end_offset <= native.size() ? native.size() - motions.end_offset : 0u) << ",\n"
              << "  \"next_compact_valid\": " << (next_compact_valid ? "true" : "false") << ",\n"
              << "  \"next_compact_value\": " << (next_compact_valid ? next_compact : 0) << ",\n"
              << "  \"next_compact_bytes\": " << (next_compact_valid ? next_compact_bytes : 0u) << "\n"
              << "}\n";
    return bone_table.valid ? 0 : 2;
}
