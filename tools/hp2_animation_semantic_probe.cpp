#include "hp2/ue_actor.h"
#include "hp2/ue_package.h"

#include <algorithm>
#include <cctype>
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
constexpr std::int32_t kMaxCount = 40000000;

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

class Cursor {
public:
    explicit Cursor(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}
    std::size_t Position() const { return position_; }
    std::size_t Remaining() const { return bytes_.size() - position_; }
    bool Skip(std::size_t count) {
        if (count > Remaining()) return false;
        position_ += count;
        return true;
    }
    bool U8(std::uint8_t& value) {
        if (Remaining() < 1u) return false;
        value = bytes_[position_++];
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
    bool Compact(std::int32_t& value) {
        std::uint8_t first = 0;
        if (!U8(first)) return false;
        const bool negative = (first & 0x80u) != 0u;
        std::uint32_t magnitude = first & 0x3fu;
        bool more = (first & 0x40u) != 0u;
        std::uint32_t shift = 6u;
        for (int index = 1; more; ++index) {
            if (index >= 5 || shift >= 32u) return false;
            std::uint8_t next = 0;
            if (!U8(next)) return false;
            const std::uint32_t payload = next & 0x7fu;
            if (shift == 27u && payload > 0x0fu) return false;
            magnitude |= payload << shift;
            shift += 7u;
            more = (next & 0x80u) != 0u;
        }
        if (magnitude > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) return false;
        value = static_cast<std::int32_t>(magnitude);
        if (negative) value = -value;
        return true;
    }
private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t position_ = 0u;
};

bool Count(Cursor& cursor, std::int32_t& value, std::int32_t limit = kMaxCount) {
    return cursor.Compact(value) && value >= 0 && value <= limit;
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
        input.seekg(entry.serial_offset, std::ios::beg);
        std::vector<std::uint8_t> payload(static_cast<std::size_t>(entry.serial_size));
        input.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
        if (input.gcount() != static_cast<std::streamsize>(payload.size())
            || properties.native_data_offset > payload.size()) return {};
        return {payload.begin() + static_cast<std::ptrdiff_t>(properties.native_data_offset), payload.end()};
    }
    return {};
}

struct Totals {
    std::uint64_t tracks = 0;
    std::uint64_t quats = 0;
    std::uint64_t positions = 0;
    std::uint64_t deltas = 0;
};

bool ReadTrackDescriptor(Cursor& cursor, Totals& totals) {
    std::uint32_t flags = 0;
    std::int32_t q = 0, p = 0, t = 0;
    float pos_scale = 0.0f, time_scale = 0.0f;
    if (!cursor.U32(flags) || !Count(cursor, q) || !Count(cursor, p) || !Count(cursor, t)
        || !cursor.F32(pos_scale) || !cursor.F32(time_scale)) return false;
    totals.tracks += 1u;
    totals.quats += static_cast<std::uint64_t>(q);
    totals.positions += static_cast<std::uint64_t>(p);
    totals.deltas += static_cast<std::uint64_t>(t);
    return true;
}

bool ReadSequence(Cursor& cursor) {
    std::int32_t name = 0, group = 0, notify_count = 0;
    std::int32_t start_frame = 0, frame_count = 0;
    float rate = 0.0f;
    if (!cursor.Compact(name) || !cursor.Compact(group)
        || !cursor.I32(start_frame) || !cursor.I32(frame_count)
        || !Count(cursor, notify_count, 1000000)) return false;
    for (std::int32_t i = 0; i < notify_count; ++i) {
        float time = 0.0f;
        std::int32_t function = 0;
        if (!cursor.F32(time) || !cursor.Compact(function)) return false;
    }
    return cursor.F32(rate);
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

    std::int32_t moves = 0;
    if (!Count(cursor, moves, 4096)) return 3;
    Totals requested;
    std::int32_t min_tracks = std::numeric_limits<std::int32_t>::max();
    std::int32_t max_tracks = 0;
    for (std::int32_t move = 0; move < moves; ++move) {
        if (!cursor.Skip(24u)) return 3;
        std::int32_t bone_indices = 0;
        if (!Count(cursor, bone_indices, 8192)
            || !cursor.Skip(static_cast<std::size_t>(bone_indices) * 4u)) return 3;
        std::int32_t tracks = 0;
        if (!Count(cursor, tracks, 8192)) return 3;
        min_tracks = std::min(min_tracks, tracks);
        max_tracks = std::max(max_tracks, tracks);
        for (std::int32_t track = 0; track < tracks; ++track) {
            if (!ReadTrackDescriptor(cursor, requested)) return 3;
        }
    }

    std::int32_t sequences = 0;
    if (!Count(cursor, sequences, 4096)) return 3;
    for (std::int32_t i = 0; i < sequences; ++i) {
        if (!ReadSequence(cursor)) return 3;
    }

    std::int32_t master_q = 0, master_p = 0, master_t = 0;
    if (!Count(cursor, master_q) || !cursor.Skip(static_cast<std::size_t>(master_q) * 6u)
        || !Count(cursor, master_p) || !cursor.Skip(static_cast<std::size_t>(master_p) * 6u)
        || !Count(cursor, master_t) || !cursor.Skip(static_cast<std::size_t>(master_t))) return 3;

    const bool pools_match = requested.quats == static_cast<std::uint64_t>(master_q)
        && requested.positions == static_cast<std::uint64_t>(master_p)
        && requested.deltas == static_cast<std::uint64_t>(master_t);
    const bool closes = cursor.Remaining() == 0u;

    std::cout << "{\n"
              << "  \"schema\":\"hp2-animation-master-track-v1\",\n"
              << "  \"file_version\":" << package.summary.file_version << ",\n"
              << "  \"licensee_version\":" << package.summary.licensee_version << ",\n"
              << "  \"native_bytes\":" << bytes.size() << ",\n"
              << "  \"bones\":" << bones << ",\n"
              << "  \"moves\":" << moves << ",\n"
              << "  \"sequences\":" << sequences << ",\n"
              << "  \"tracks\":" << requested.tracks << ",\n"
              << "  \"track_min\":" << (min_tracks == std::numeric_limits<std::int32_t>::max() ? 0 : min_tracks) << ",\n"
              << "  \"track_max\":" << max_tracks << ",\n"
              << "  \"requested_quaternions\":" << requested.quats << ",\n"
              << "  \"requested_positions\":" << requested.positions << ",\n"
              << "  \"requested_deltas\":" << requested.deltas << ",\n"
              << "  \"master_quaternions\":" << master_q << ",\n"
              << "  \"master_positions\":" << master_p << ",\n"
              << "  \"master_deltas\":" << master_t << ",\n"
              << "  \"pools_match\":" << (pools_match ? "true" : "false") << ",\n"
              << "  \"remaining_bytes\":" << cursor.Remaining() << ",\n"
              << "  \"closes\":" << (closes ? "true" : "false") << "\n"
              << "}\n";
    return pools_match && closes ? 0 : 5;
}
