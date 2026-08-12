#include "hp2/ue_package.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

std::string JsonEscape(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (const char c : value) {
        switch (c) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) >= 0x20) {
                    result += c;
                }
                break;
        }
    }
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: hp2_probe <extracted-hp2-directory>\n";
        return 64;
    }

    const std::filesystem::path root = argv[1];
    const auto packages = hp2::ScanPackages(root);
    std::size_t valid_count = 0;
    for (const auto& package : packages) {
        valid_count += package.valid ? 1u : 0u;
    }

    std::cout << "{\n"
              << "  \"schema\": \"hp2-package-probe-v1\",\n"
              << "  \"root\": \"" << JsonEscape(root.generic_string()) << "\",\n"
              << "  \"candidate_count\": " << packages.size() << ",\n"
              << "  \"valid_count\": " << valid_count << ",\n"
              << "  \"packages\": [\n";

    for (std::size_t index = 0; index < packages.size(); ++index) {
        const auto& package = packages[index];
        std::error_code error_code;
        const auto relative = std::filesystem::relative(package.path, root, error_code);
        const std::string display_path = error_code
            ? package.path.generic_string()
            : relative.generic_string();

        std::cout << "    {\"path\": \"" << JsonEscape(display_path)
                  << "\", \"valid\": " << (package.valid ? "true" : "false")
                  << ", \"size\": " << package.file_size
                  << ", \"version\": " << package.file_version
                  << ", \"licensee_version\": " << package.licensee_version
                  << ", \"names\": " << package.name_count
                  << ", \"imports\": " << package.import_count
                  << ", \"exports\": " << package.export_count;
        if (!package.error.empty()) {
            std::cout << ", \"error\": \"" << JsonEscape(package.error) << "\"";
        }
        std::cout << "}" << (index + 1 == packages.size() ? "" : ",") << "\n";
    }

    std::cout << "  ]\n}\n";
    return valid_count > 0 ? 0 : 2;
}
