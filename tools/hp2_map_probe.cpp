#include "hp2/ue_package.h"

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

std::string JsonEscape(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (const char character : value) {
        switch (character) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (static_cast<unsigned char>(character) >= 0x20u) {
                    result += character;
                }
                break;
        }
    }
    return result;
}

void PrintExport(const hp2::ExportEntry& entry, std::size_t index, const char* indentation) {
    std::cout << indentation
              << "{\"index\": " << index
              << ", \"object_name\": \"" << JsonEscape(entry.object_name)
              << "\", \"class_name\": \"" << JsonEscape(entry.class_name)
              << "\", \"class_index\": " << entry.class_index
              << ", \"super_index\": " << entry.super_index
              << ", \"outer_index\": " << entry.package_index
              << ", \"object_flags\": " << entry.object_flags
              << ", \"serial_size\": " << entry.serial_size
              << ", \"serial_offset\": " << entry.serial_offset
              << "}";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: hp2_map_probe <map.unr>\n";
        return 64;
    }

    const std::filesystem::path map_path = argv[1];
    const hp2::PackageIndex package = hp2::LoadPackageIndex(map_path);
    if (!package.valid) {
        std::cerr << "Cannot parse map package index: " << package.error << '\n';
        return 2;
    }

    std::size_t geometry_count = 0;
    for (const auto& entry : package.exports) {
        geometry_count += hp2::IsGeometryCandidate(entry) ? 1u : 0u;
    }

    std::cout << "{\n"
              << "  \"schema\": \"hp2-map-index-v1\",\n"
              << "  \"path\": \"" << JsonEscape(map_path.generic_string()) << "\",\n"
              << "  \"version\": " << package.summary.file_version << ",\n"
              << "  \"licensee_version\": " << package.summary.licensee_version << ",\n"
              << "  \"name_count\": " << package.names.size() << ",\n"
              << "  \"import_count\": " << package.imports.size() << ",\n"
              << "  \"export_count\": " << package.exports.size() << ",\n"
              << "  \"geometry_candidate_count\": " << geometry_count << ",\n"
              << "  \"imports\": [\n";

    for (std::size_t index = 0; index < package.imports.size(); ++index) {
        const auto& entry = package.imports[index];
        std::cout << "    {\"index\": " << index
                  << ", \"class_package\": \"" << JsonEscape(entry.class_package)
                  << "\", \"class_name\": \"" << JsonEscape(entry.class_name)
                  << "\", \"outer_index\": " << entry.package_index
                  << ", \"object_name\": \"" << JsonEscape(entry.object_name) << "\"}"
                  << (index + 1 == package.imports.size() ? "" : ",") << '\n';
    }

    std::cout << "  ],\n  \"geometry_candidates\": [\n";
    std::size_t printed_geometry = 0;
    for (std::size_t index = 0; index < package.exports.size(); ++index) {
        if (!hp2::IsGeometryCandidate(package.exports[index])) {
            continue;
        }
        PrintExport(package.exports[index], index, "    ");
        ++printed_geometry;
        std::cout << (printed_geometry == geometry_count ? "" : ",") << '\n';
    }

    std::cout << "  ],\n  \"exports\": [\n";
    for (std::size_t index = 0; index < package.exports.size(); ++index) {
        PrintExport(package.exports[index], index, "    ");
        std::cout << (index + 1 == package.exports.size() ? "" : ",") << '\n';
    }
    std::cout << "  ]\n}\n";
    return geometry_count > 0 ? 0 : 3;
}
