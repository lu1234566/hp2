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
    bool Skip(std::size_t count) {
        if (count > Remaining()) return false;
        position_ += count;
        return true;
    }
private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t position_ = 0;
};

bool Count(Cursor& cursor, std::int32_t& value, std::int32_t max_count = kMaxCount) {
    return cursor.Compact(value) && value >= 0 && value <= max_count;
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

std::size_t MotionStart(const std::vector<std::uint8_t>& bytes) {
    Cursor cursor(bytes);
    std::int32_t bones = 0;
    if (!Count(cursor, bones, 4096)) return 0u;
    for (std::int32_t i = 0; i < bones; ++i) {
        std::int32_t name = 0;
        if (!cursor.Compact(name) || !cursor.Skip(8u)) return 0u;
    }
    return cursor.Position();
}

struct Candidate {
    bool lazy_q = false;
    bool lazy_p = false;
    bool lazy_t = false;
};

struct Result {
    Candidate candidate;
    bool valid = false;
    std::string error;
    std::size_t offset = 0;
    std::int32_t moves = 0;
    std::int32_t complete_moves = 0;
    std::uint64_t complete_tracks = 0;
    std::int32_t failed_move = -1;
    std::int32_t failed_track = -1;
    std::uint64_t q_keys = 0;
    std::uint64_t p_keys = 0;
    std::uint64_t t_keys = 0;
};

bool Array(Cursor& cursor, bool lazy, std::size_t stride, std::uint64_t& key_total, const char* label,
           std::string& error) {
    if (lazy && !cursor.Skip(4u)) { error = std::string(label) + "_lazy_prefix"; return false; }
    std::int32_t count = 0;
    if (!Count(cursor, count)) { error = std::string(label) + "_count"; return false; }
    const std::uint64_t bytes = static_cast<std::uint64_t>(count) * stride;
    if (bytes > cursor.Remaining() || !cursor.Skip(static_cast<std::size_t>(bytes))) {
        error = std::string(label) + "_data";
        return false;
    }
    key_total += static_cast<std::uint64_t>(count);
    return true;
}

bool Track(Cursor& cursor, const Candidate& candidate, Result& result) {
    if (!cursor.Skip(4u)) { result.error = "track_flags"; return false; }
    if (!Array(cursor, candidate.lazy_q, 16u, result.q_keys, "quat", result.error)) return false;
    if (!Array(cursor, candidate.lazy_p, 12u, result.p_keys, "pos", result.error)) return false;
    if (!Array(cursor, candidate.lazy_t, 4u, result.t_keys, "time", result.error)) return false;
    return true;
}

Result Evaluate(const std::vector<std::uint8_t>& bytes, std::size_t start, Candidate candidate) {
    Result result;
    result.candidate = candidate;
    Cursor cursor(bytes);
    if (!cursor.Skip(start) || !Count(cursor, result.moves, 4096)) {
        result.error = "moves_count";
        result.offset = cursor.Position();
        return result;
    }

    for (std::int32_t move = 0; move < result.moves; ++move) {
        result.failed_move = move;
        if (!cursor.Skip(24u)) { result.error = "motion_header"; result.offset = cursor.Position(); return result; }

        std::int32_t bone_indices = 0;
        if (!Count(cursor, bone_indices, 4096)) {
            result.error = "bone_indices_count"; result.offset = cursor.Position(); return result;
        }
        const std::uint64_t bone_bytes = static_cast<std::uint64_t>(bone_indices) * 4u;
        if (bone_bytes > cursor.Remaining() || !cursor.Skip(static_cast<std::size_t>(bone_bytes))) {
            result.error = "bone_indices_data"; result.offset = cursor.Position(); return result;
        }

        std::int32_t tracks = 0;
        if (!Count(cursor, tracks, 4096)) {
            result.error = "track_count"; result.offset = cursor.Position(); return result;
        }
        for (std::int32_t track = 0; track < tracks; ++track) {
            result.failed_track = track;
            if (!Track(cursor, candidate, result)) { result.offset = cursor.Position(); return result; }
            ++result.complete_tracks;
        }

        result.failed_track = tracks;
        if (!Track(cursor, candidate, result)) {
            result.error = "root_" + result.error;
            result.offset = cursor.Position();
            return result;
        }
        ++result.complete_tracks;
        ++result.complete_moves;
    }

    result.valid = true;
    result.failed_move = -1;
    result.failed_track = -1;
    result.offset = cursor.Position();
    return result;
}

const char* Bool(bool value) { return value ? "true" : "false"; }
}

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

    std::vector<Result> results;
    for (unsigned mask = 0; mask < 8u; ++mask) {
        Candidate candidate{(mask & 1u) != 0u, (mask & 2u) != 0u, (mask & 4u) != 0u};
        results.push_back(Evaluate(bytes, start, candidate));
    }
    std::sort(results.begin(), results.end(), [](const Result& a, const Result& b) {
        if (a.valid != b.valid) return a.valid > b.valid;
        if (a.complete_moves != b.complete_moves) return a.complete_moves > b.complete_moves;
        if (a.complete_tracks != b.complete_tracks) return a.complete_tracks > b.complete_tracks;
        return a.offset > b.offset;
    });

    std::cout << "{\n  \"schema\": \"hp2-animation-lazy2-probe-v1\",\n"
              << "  \"native_bytes\": " << bytes.size() << ",\n"
              << "  \"motion_start\": " << start << ",\n"
              << "  \"candidates\": [\n";
    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        std::cout << "    {\"lazy_q\":" << Bool(r.candidate.lazy_q)
                  << ",\"lazy_p\":" << Bool(r.candidate.lazy_p)
                  << ",\"lazy_t\":" << Bool(r.candidate.lazy_t)
                  << ",\"valid\":" << Bool(r.valid)
                  << ",\"moves\":" << r.moves
                  << ",\"complete_moves\":" << r.complete_moves
                  << ",\"complete_tracks\":" << r.complete_tracks
                  << ",\"failed_move\":" << r.failed_move
                  << ",\"failed_track\":" << r.failed_track
                  << ",\"error\":\"" << r.error << "\""
                  << ",\"offset\":" << r.offset
                  << ",\"q_keys\":" << r.q_keys
                  << ",\"p_keys\":" << r.p_keys
                  << ",\"t_keys\":" << r.t_keys << "}"
                  << (i + 1u == results.size() ? "" : ",") << '\n';
    }
    std::cout << "  ]\n}\n";
    return 0;
}
