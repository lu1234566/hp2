#include "hp2/ue_package.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <system_error>

namespace hp2 {
namespace {

constexpr std::size_t kSummaryBytes = 36;
constexpr std::int32_t kMaxReasonableEntries = 2'000'000;

std::uint16_t ReadU16(const std::array<std::uint8_t, kSummaryBytes>& data, std::size_t offset) {
    return static_cast<std::uint16_t>(data[offset])
        | (static_cast<std::uint16_t>(data[offset + 1]) << 8u);
}

std::uint32_t ReadU32(const std::array<std::uint8_t, kSummaryBytes>& data, std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset])
        | (static_cast<std::uint32_t>(data[offset + 1]) << 8u)
        | (static_cast<std::uint32_t>(data[offset + 2]) << 16u)
        | (static_cast<std::uint32_t>(data[offset + 3]) << 24u);
}

std::int32_t ReadI32(const std::array<std::uint8_t, kSummaryBytes>& data, std::size_t offset) {
    return static_cast<std::int32_t>(ReadU32(data, offset));
}

bool TableLooksSane(std::int32_t count, std::int32_t offset, std::uintmax_t size) {
    if (count < 0 || count > kMaxReasonableEntries || offset < 0) {
        return false;
    }
    if (count == 0) {
        return static_cast<std::uintmax_t>(offset) <= size;
    }
    return static_cast<std::uintmax_t>(offset) >= kSummaryBytes
        && static_cast<std::uintmax_t>(offset) < size;
}

std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

}  // namespace

bool IsPackageExtension(const std::filesystem::path& path) {
    const std::string ext = Lowercase(path.extension().string());
    return ext == ".u" || ext == ".utx" || ext == ".unr" || ext == ".uax" || ext == ".umx";
}

PackageSummary ProbePackage(const std::filesystem::path& path) {
    PackageSummary result;
    result.path = path;

    std::error_code error_code;
    result.file_size = std::filesystem::file_size(path, error_code);
    if (error_code) {
        result.error = "cannot read file size: " + error_code.message();
        return result;
    }
    if (result.file_size < kSummaryBytes) {
        result.error = "file is smaller than an Unreal package summary";
        return result;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        result.error = "cannot open file";
        return result;
    }

    std::array<std::uint8_t, kSummaryBytes> bytes{};
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
        result.error = "short read while loading package summary";
        return result;
    }

    if (ReadU32(bytes, 0) != kUnrealPackageTag) {
        result.error = "invalid Unreal package tag";
        return result;
    }

    result.file_version = ReadU16(bytes, 4);
    result.licensee_version = ReadU16(bytes, 6);
    result.package_flags = ReadU32(bytes, 8);
    result.name_count = ReadI32(bytes, 12);
    result.name_offset = ReadI32(bytes, 16);
    result.export_count = ReadI32(bytes, 20);
    result.export_offset = ReadI32(bytes, 24);
    result.import_count = ReadI32(bytes, 28);
    result.import_offset = ReadI32(bytes, 32);

    if (!TableLooksSane(result.name_count, result.name_offset, result.file_size)) {
        result.error = "invalid name table bounds";
        return result;
    }
    if (!TableLooksSane(result.export_count, result.export_offset, result.file_size)) {
        result.error = "invalid export table bounds";
        return result;
    }
    if (!TableLooksSane(result.import_count, result.import_offset, result.file_size)) {
        result.error = "invalid import table bounds";
        return result;
    }

    result.valid = true;
    return result;
}

std::vector<PackageSummary> ScanPackages(
    const std::filesystem::path& root,
    std::size_t max_packages
) {
    std::vector<PackageSummary> packages;
    std::error_code error_code;
    if (!std::filesystem::is_directory(root, error_code)) {
        return packages;
    }

    const auto options = std::filesystem::directory_options::skip_permission_denied;
    std::filesystem::recursive_directory_iterator it(root, options, error_code);
    const std::filesystem::recursive_directory_iterator end;
    while (it != end && packages.size() < max_packages) {
        if (error_code) {
            error_code.clear();
            it.increment(error_code);
            continue;
        }
        if (it->is_regular_file(error_code) && !error_code && IsPackageExtension(it->path())) {
            packages.push_back(ProbePackage(it->path()));
        }
        it.increment(error_code);
    }

    std::sort(packages.begin(), packages.end(), [](const PackageSummary& left, const PackageSummary& right) {
        return left.path.generic_string() < right.path.generic_string();
    });
    return packages;
}

}  // namespace hp2
