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

bool CompactAt(
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
        if (!CompactAt(bytes_, position_, value, used)) return false;
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
    std::size_t position_ = 0;
};

bool Count(Cursor& cursor, std::int32_t& value) {
    return cursor.Compact(value) && value >= 0 && value <= kMaxCount;
}

std::vector<std::uint8_t> NativePayload(
    const hp2::PackageIndex& package,
    const std::string& object_name
) {
    for (std::size_t index = 0; index < package.exports.size(); ++index) {
        const auto& entry = package.exports[index];
        if (Lower(entry.class_name) != "animation" || Lower(entry.object_name) != Lower(object_name)) continue;
        const hp2::ObjectProperties properties = hp2::LoadObjectProperties(package, index);
        if (!properties.valid || entry.serial_size <= 0 || entry.serial_offset < 0) return {};
        std::ifstream input(package.summary.path, std::ios::binary);
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

std::size_t MotionStart(const std::vector<std::uint8_t>& bytes) {
    Cursor cursor(bytes, 0u);
    std::int32_t bones = 0;
    if (!Count(cursor, bones) || bones > 4096) return 0u;
    for (std::int32_t index = 0; index < bones; ++index) {
        std::int32_t name = 0;
        if (!cursor.Compact(name) || !cursor.Skip(8u)) return 0u;
    }
    return cursor.Position();
}

struct ArraySpec {
    char kind = '?';
    std::size_t stride = 0;
    std::size_t prefix = 0;
};

struct Layout {
    const char* name = "";
    bool flags = true;
    std::vector<ArraySpec> arrays;
};

struct Result {
    std::string name;
    bool valid = false;
    std::string error;
    std::int32_t moves = 0;
    std::int32_t complete_moves = 0;
    std::uint64_t complete_tracks = 0;
    std::int32_t failed_move = -1;
    std::int32_t failed_track = -1;
    std::size_t offset = 0;
    std::size_t end = 0;
    std::size_t remaining = 0;
    std::uint64_t q = 0;
    std::uint64_t p = 0;
    std::uint64_t t = 0;
    std::int32_t track_min = std::numeric_limits<std::int32_t>::max();
    std::int32_t track_max = 0;
    std::size_t tracks135 = 0;
};

Result Evaluate(const std::vector<std::uint8_t>& bytes, std::size_t start, const Layout& layout) {
    Result result;
    result.name = layout.name;
    Cursor cursor(bytes, start);
    if (!Count(cursor, result.moves) || result.moves > 4096) {
        result.error = "moves";
        result.offset = cursor.Position();
        return result;
    }
    for (std::int32_t move = 0; move < result.moves; ++move) {
        result.failed_move = move;
        if (!cursor.Skip(24u)) {
            result.error = "header";
            result.offset = cursor.Position();
            return result;
        }
        std::int32_t tracks = 0;
        if (!Count(cursor, tracks) || tracks > 4096) {
            result.error = "tracks";
            result.offset = cursor.Position();
            return result;
        }
        result.track_min = std::min(result.track_min, tracks);
        result.track_max = std::max(result.track_max, tracks);
        result.tracks135 += tracks == 135 ? 1u : 0u;
        for (std::int32_t track = 0; track < tracks; ++track) {
            result.failed_track = track;
            if (layout.flags && !cursor.Skip(4u)) {
                result.error = "flags";
                result.offset = cursor.Position();
                return result;
            }
            for (const auto& array : layout.arrays) {
                if (!cursor.Skip(array.prefix)) {
                    result.error = std::string("prefix_") + array.kind;
                    result.offset = cursor.Position();
                    return result;
                }
                std::int32_t count = 0;
                if (!Count(cursor, count)) {
                    result.error = std::string("count_") + array.kind;
                    result.offset = cursor.Position();
                    return result;
                }
                const std::uint64_t byte_count = static_cast<std::uint64_t>(count) * array.stride;
                if (byte_count > cursor.Remaining() || !cursor.Skip(static_cast<std::size_t>(byte_count))) {
                    result.error = std::string("data_") + array.kind;
                    result.offset = cursor.Position();
                    return result;
                }
                if (array.kind == 'q') result.q += static_cast<std::uint64_t>(count);
                if (array.kind == 'p') result.p += static_cast<std::uint64_t>(count);
                if (array.kind == 't') result.t += static_cast<std::uint64_t>(count);
            }
            ++result.complete_tracks;
        }
        ++result.complete_moves;
    }
    if (result.track_min == std::numeric_limits<std::int32_t>::max()) result.track_min = 0;
    result.failed_move = -1;
    result.failed_track = -1;
    result.valid = true;
    result.end = cursor.Position();
    result.remaining = cursor.Remaining();
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) return 64;
    const auto package_path = FindPackage(argv[1], argv[2]);
    if (package_path.empty()) return 2;
    const auto package = hp2::LoadPackageIndex(package_path);
    if (!package.valid) return 2;
    const auto bytes = NativePayload(package, argv[3]);
    if (bytes.empty()) return 2;
    const std::size_t start = MotionStart(bytes);
    if (start == 0u) return 2;

    const std::vector<Layout> layouts = {
        {"flags_lq16_lp12_lt4", true, {{'q',16u,4u},{'p',12u,4u},{'t',4u,4u}}},
        {"flags_lq20_lp12_lt4", true, {{'q',20u,4u},{'p',12u,4u},{'t',4u,4u}}},
        {"flags_lq16_lp12_t4", true, {{'q',16u,4u},{'p',12u,4u},{'t',4u,0u}}},
        {"flags_lq20_lp12_t4", true, {{'q',20u,4u},{'p',12u,4u},{'t',4u,0u}}},
        {"flags_lq16_p12_lt4", true, {{'q',16u,4u},{'p',12u,0u},{'t',4u,4u}}},
        {"flags_q16_lp12_lt4", true, {{'q',16u,0u},{'p',12u,4u},{'t',4u,4u}}},
        {"flags_lq16_lp12", true, {{'q',16u,4u},{'p',12u,4u}}},
        {"flags_lq20_lp12", true, {{'q',20u,4u},{'p',12u,4u}}},
        {"flags_lp12_lq16_lt4", true, {{'p',12u,4u},{'q',16u,4u},{'t',4u,4u}}},
        {"flags_lq16_lt4_lp12", true, {{'q',16u,4u},{'t',4u,4u},{'p',12u,4u}}},
        {"lq16_lp12_lt4", false, {{'q',16u,4u},{'p',12u,4u},{'t',4u,4u}}},
    };

    std::vector<Result> results;
    for (const auto& layout : layouts) results.push_back(Evaluate(bytes, start, layout));
    std::sort(results.begin(), results.end(), [](const Result& a, const Result& b) {
        if (a.valid != b.valid) return a.valid > b.valid;
        if (a.complete_moves != b.complete_moves) return a.complete_moves > b.complete_moves;
        if (a.complete_tracks != b.complete_tracks) return a.complete_tracks > b.complete_tracks;
        return a.offset > b.offset;
    });

    std::cout << "{\n  \"schema\": \"hp2-animation-lazy-probe-v1\",\n"
              << "  \"native_bytes\": " << bytes.size() << ",\n"
              << "  \"motion_start\": " << start << ",\n"
              << "  \"layouts\": [\n";
    for (std::size_t index = 0; index < results.size(); ++index) {
        const auto& r = results[index];
        std::cout << "    {\"name\":\"" << r.name
                  << "\",\"valid\":" << (r.valid ? "true" : "false")
                  << ",\"moves\":" << r.moves
                  << ",\"complete_moves\":" << r.complete_moves
                  << ",\"complete_tracks\":" << r.complete_tracks
                  << ",\"failed_move\":" << r.failed_move
                  << ",\"failed_track\":" << r.failed_track
                  << ",\"error\":\"" << r.error
                  << "\",\"offset\":" << r.offset
                  << ",\"end\":" << r.end
                  << ",\"remaining\":" << r.remaining
                  << ",\"track_min\":" << r.track_min
                  << ",\"track_max\":" << r.track_max
                  << ",\"moves_135\":" << r.tracks135
                  << ",\"q_keys\":" << r.q
                  << ",\"p_keys\":" << r.p
                  << ",\"t_keys\":" << r.t << "}"
                  << (index + 1u == results.size() ? "" : ",") << '\n';
    }
    std::cout << "  ]\n}\n";
    return 0;
}
