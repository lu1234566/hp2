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

std::uint32_t ReadU32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset + 4u > bytes.size()) return 0u;
    return static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1u]) << 8u)
        | (static_cast<std::uint32_t>(bytes[offset + 2u]) << 16u)
        | (static_cast<std::uint32_t>(bytes[offset + 3u]) << 24u);
}

struct BoneChannelTable {
    bool valid = false;
    std::int32_t count = 0;
    std::size_t start_offset = 0;
    std::size_t end_offset = 0;
    std::size_t valid_name_count = 0;
    std::size_t metadata_a_nonzero = 0;
    std::size_t metadata_b_nonzero = 0;
    std::uint32_t metadata_a_max = 0;
    std::uint32_t metadata_b_max = 0;
    std::vector<std::string> first_names;
    std::vector<std::string> last_names;
};

BoneChannelTable ParseBoneChannelTable(
    const hp2::PackageIndex& package,
    const std::vector<std::uint8_t>& native
) {
    BoneChannelTable result;
    std::size_t position = 0;
    std::size_t used = 0;
    if (!DecodeCompact(native, position, result.count, used)
        || result.count < 0 || result.count > 4096) {
        return result;
    }
    position += used;
    result.start_offset = position;
    std::vector<std::string> all_names;
    all_names.reserve(static_cast<std::size_t>(result.count));
    for (std::int32_t index = 0; index < result.count; ++index) {
        std::int32_t name_index = 0;
        if (!DecodeCompact(native, position, name_index, used)) {
            return result;
        }
        position += used;
        if (position + 8u > native.size()) {
            return result;
        }
        std::string name;
        if (name_index >= 0 && static_cast<std::size_t>(name_index) < package.names.size()) {
            name = package.names[static_cast<std::size_t>(name_index)].value;
            ++result.valid_name_count;
        }
        all_names.push_back(name);
        const std::uint32_t metadata_a = ReadU32(native, position);
        const std::uint32_t metadata_b = ReadU32(native, position + 4u);
        result.metadata_a_nonzero += metadata_a != 0u ? 1u : 0u;
        result.metadata_b_nonzero += metadata_b != 0u ? 1u : 0u;
        result.metadata_a_max = std::max(result.metadata_a_max, metadata_a);
        result.metadata_b_max = std::max(result.metadata_b_max, metadata_b);
        position += 8u;
    }
    result.end_offset = position;
    const std::size_t sample = std::min<std::size_t>(12u, all_names.size());
    result.first_names.assign(all_names.begin(), all_names.begin() + static_cast<std::ptrdiff_t>(sample));
    if (all_names.size() > sample) {
        result.last_names.assign(
            all_names.end() - static_cast<std::ptrdiff_t>(sample), all_names.end()
        );
    } else {
        result.last_names = result.first_names;
    }
    result.valid = result.valid_name_count == static_cast<std::size_t>(result.count);
    return result;
}

struct CompactCandidate {
    std::size_t relative_offset = 0;
    std::size_t absolute_offset = 0;
    std::size_t bytes = 0;
    std::int32_t value = 0;
    std::string name;
};

std::vector<CompactCandidate> ScanAfterOffset(
    const hp2::PackageIndex& package,
    const std::vector<std::uint8_t>& native,
    std::size_t start,
    std::size_t byte_count
) {
    std::vector<CompactCandidate> result;
    if (start >= native.size()) return result;
    const std::size_t end = std::min(native.size(), start + byte_count);
    for (std::size_t offset = start; offset < end; ++offset) {
        std::int32_t value = 0;
        std::size_t used = 0;
        if (!DecodeCompact(native, offset, value, used)) continue;
        CompactCandidate candidate;
        candidate.relative_offset = offset - start;
        candidate.absolute_offset = offset;
        candidate.bytes = used;
        candidate.value = value;
        if (value >= 0 && static_cast<std::size_t>(value) < package.names.size()) {
            candidate.name = package.names[static_cast<std::size_t>(value)].value;
        }
        if (std::abs(static_cast<std::int64_t>(value)) <= 100000 || !candidate.name.empty()) {
            result.push_back(std::move(candidate));
        }
    }
    if (result.size() > 96u) result.resize(96u);
    return result;
}

void PrintStringArray(const std::vector<std::string>& values) {
    std::cout << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0u) std::cout << ", ";
        std::cout << '"' << JsonEscape(values[index]) << '"';
    }
    std::cout << ']';
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: hp2_animation_probe <game-root> <package> <animation-object>\n";
        return 64;
    }

    const std::filesystem::path package_path = FindPackage(argv[1], argv[2]);
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
    std::int32_t after_bones_compact = 0;
    std::size_t after_bones_compact_bytes = 0;
    const bool after_bones_compact_valid = bone_table.valid
        && DecodeCompact(
            native, bone_table.end_offset, after_bones_compact, after_bones_compact_bytes
        );
    const auto after_bones_candidates = ScanAfterOffset(
        package, native, bone_table.end_offset, 256u
    );

    std::cout << "{\n"
              << "  \"schema\": \"hp2-animation-probe-v2\",\n"
              << "  \"package\": \"" << JsonEscape(package.summary.path.stem().string()) << "\",\n"
              << "  \"object\": \"" << JsonEscape(entry.object_name) << "\",\n"
              << "  \"class\": \"" << JsonEscape(entry.class_name) << "\",\n"
              << "  \"version\": " << package.summary.file_version << ",\n"
              << "  \"serial_size\": " << entry.serial_size << ",\n"
              << "  \"native_offset\": " << properties.native_data_offset << ",\n"
              << "  \"native_bytes\": " << native.size() << ",\n"
              << "  \"property_count\": " << properties.properties.size() << ",\n"
              << "  \"bone_table_valid\": " << (bone_table.valid ? "true" : "false") << ",\n"
              << "  \"bone_channel_count\": " << bone_table.count << ",\n"
              << "  \"bone_table_start_offset\": " << bone_table.start_offset << ",\n"
              << "  \"bone_table_end_offset\": " << bone_table.end_offset << ",\n"
              << "  \"bone_valid_names\": " << bone_table.valid_name_count << ",\n"
              << "  \"bone_metadata_a_nonzero\": " << bone_table.metadata_a_nonzero << ",\n"
              << "  \"bone_metadata_b_nonzero\": " << bone_table.metadata_b_nonzero << ",\n"
              << "  \"bone_metadata_a_max\": " << bone_table.metadata_a_max << ",\n"
              << "  \"bone_metadata_b_max\": " << bone_table.metadata_b_max << ",\n"
              << "  \"bone_first_names\": ";
    PrintStringArray(bone_table.first_names);
    std::cout << ",\n  \"bone_last_names\": ";
    PrintStringArray(bone_table.last_names);
    std::cout << ",\n"
              << "  \"after_bones_compact_valid\": "
              << (after_bones_compact_valid ? "true" : "false") << ",\n"
              << "  \"after_bones_compact_value\": "
              << (after_bones_compact_valid ? after_bones_compact : 0) << ",\n"
              << "  \"after_bones_compact_bytes\": "
              << (after_bones_compact_valid ? after_bones_compact_bytes : 0) << ",\n"
              << "  \"bytes_after_bone_table\": "
              << (bone_table.end_offset <= native.size() ? native.size() - bone_table.end_offset : 0u)
              << ",\n"
              << "  \"after_bones_candidates\": [\n";
    for (std::size_t index = 0; index < after_bones_candidates.size(); ++index) {
        const auto& candidate = after_bones_candidates[index];
        std::cout << "    {\"relative_offset\": " << candidate.relative_offset
                  << ", \"absolute_offset\": " << candidate.absolute_offset
                  << ", \"bytes\": " << candidate.bytes
                  << ", \"value\": " << candidate.value
                  << ", \"name\": \"" << JsonEscape(candidate.name) << "\"}"
                  << (index + 1u == after_bones_candidates.size() ? "" : ",") << '\n';
    }
    std::cout << "  ]\n}\n";
    return bone_table.valid ? 0 : 2;
}
