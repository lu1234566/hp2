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

struct CompactCandidate {
    std::size_t offset = 0;
    std::size_t bytes = 0;
    std::int32_t value = 0;
    std::string name;
    bool export_ref = false;
    bool import_ref = false;
    std::string ref_class;
    std::string ref_object;
};

std::vector<CompactCandidate> ScanCompactCandidates(
    const hp2::PackageIndex& package,
    const std::vector<std::uint8_t>& native,
    std::size_t max_bytes
) {
    std::vector<CompactCandidate> result;
    const std::size_t limit = std::min(max_bytes, native.size());
    for (std::size_t offset = 0; offset < limit; ++offset) {
        std::int32_t value = 0;
        std::size_t used = 0;
        if (!DecodeCompact(native, offset, value, used)) continue;
        CompactCandidate candidate;
        candidate.offset = offset;
        candidate.bytes = used;
        candidate.value = value;
        if (value >= 0 && static_cast<std::size_t>(value) < package.names.size()) {
            candidate.name = package.names[static_cast<std::size_t>(value)].value;
        }
        if (value > 0) {
            const std::size_t index = static_cast<std::size_t>(value - 1);
            if (index < package.exports.size()) {
                candidate.export_ref = true;
                candidate.ref_class = package.exports[index].class_name;
                candidate.ref_object = package.exports[index].object_name;
            }
        } else if (value < 0) {
            const std::size_t index = static_cast<std::size_t>(-static_cast<std::int64_t>(value) - 1);
            if (index < package.imports.size()) {
                candidate.import_ref = true;
                candidate.ref_class = package.imports[index].class_name;
                candidate.ref_object = package.imports[index].object_name;
            }
        }
        if (std::abs(static_cast<std::int64_t>(value)) <= 100000
            || !candidate.name.empty() || candidate.export_ref || candidate.import_ref) {
            result.push_back(std::move(candidate));
        }
    }
    if (result.size() > 128u) result.resize(128u);
    return result;
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
    std::vector<std::uint8_t> native(
        payload.begin() + static_cast<std::ptrdiff_t>(properties.native_data_offset), payload.end()
    );

    std::int32_t first_compact = 0;
    std::size_t first_compact_bytes = 0;
    const bool first_compact_valid = DecodeCompact(native, 0u, first_compact, first_compact_bytes);
    const auto candidates = ScanCompactCandidates(package, native, 192u);

    std::vector<std::string> relevant_names;
    for (const auto& name : package.names) {
        const std::string lower = Lowercase(name.value);
        if (lower.find("anim") != std::string::npos
            || lower.find("bone") != std::string::npos
            || lower.find("track") != std::string::npos
            || lower.find("key") != std::string::npos
            || lower.find("move") != std::string::npos) {
            relevant_names.push_back(name.value);
        }
    }
    if (relevant_names.size() > 128u) relevant_names.resize(128u);

    std::cout << "{\n"
              << "  \"schema\": \"hp2-animation-probe-v1\",\n"
              << "  \"package\": \"" << JsonEscape(package.summary.path.stem().string()) << "\",\n"
              << "  \"object\": \"" << JsonEscape(entry.object_name) << "\",\n"
              << "  \"class\": \"" << JsonEscape(entry.class_name) << "\",\n"
              << "  \"version\": " << package.summary.file_version << ",\n"
              << "  \"serial_size\": " << entry.serial_size << ",\n"
              << "  \"native_offset\": " << properties.native_data_offset << ",\n"
              << "  \"native_bytes\": " << native.size() << ",\n"
              << "  \"property_count\": " << properties.properties.size() << ",\n"
              << "  \"first_compact_valid\": " << (first_compact_valid ? "true" : "false") << ",\n"
              << "  \"first_compact_value\": " << (first_compact_valid ? first_compact : 0) << ",\n"
              << "  \"first_compact_bytes\": " << (first_compact_valid ? first_compact_bytes : 0) << ",\n"
              << "  \"native_first_u32\": " << ReadU32(native, 0u) << ",\n"
              << "  \"properties\": [\n";
    for (std::size_t index = 0; index < properties.properties.size(); ++index) {
        const auto& property = properties.properties[index];
        std::cout << "    {\"name\": \"" << JsonEscape(property.name)
                  << "\", \"struct\": \"" << JsonEscape(property.struct_name)
                  << "\", \"type\": " << static_cast<unsigned>(property.type)
                  << ", \"array_index\": " << property.array_index
                  << ", \"bytes\": " << property.bytes.size() << "}"
                  << (index + 1u == properties.properties.size() ? "" : ",") << '\n';
    }
    std::cout << "  ],\n"
              << "  \"compact_candidates\": [\n";
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        const auto& candidate = candidates[index];
        std::cout << "    {\"offset\": " << candidate.offset
                  << ", \"bytes\": " << candidate.bytes
                  << ", \"value\": " << candidate.value
                  << ", \"name\": \"" << JsonEscape(candidate.name)
                  << "\", \"ref_class\": \"" << JsonEscape(candidate.ref_class)
                  << "\", \"ref_object\": \"" << JsonEscape(candidate.ref_object) << "\"}"
                  << (index + 1u == candidates.size() ? "" : ",") << '\n';
    }
    std::cout << "  ],\n"
              << "  \"relevant_names\": [";
    for (std::size_t index = 0; index < relevant_names.size(); ++index) {
        if (index != 0u) std::cout << ", ";
        std::cout << "\"" << JsonEscape(relevant_names[index]) << "\"";
    }
    std::cout << "]\n}\n";
    return 0;
}
