#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace hp2 {

constexpr std::uint32_t kUnrealPackageTag = 0x9E2A83C1u;

struct PackageSummary {
    std::filesystem::path path;
    std::uintmax_t file_size = 0;
    std::uint16_t file_version = 0;
    std::uint16_t licensee_version = 0;
    std::uint32_t package_flags = 0;
    std::int32_t name_count = 0;
    std::int32_t name_offset = 0;
    std::int32_t export_count = 0;
    std::int32_t export_offset = 0;
    std::int32_t import_count = 0;
    std::int32_t import_offset = 0;
    bool valid = false;
    std::string error;
};

bool IsPackageExtension(const std::filesystem::path& path);
PackageSummary ProbePackage(const std::filesystem::path& path);
std::vector<PackageSummary> ScanPackages(
    const std::filesystem::path& root,
    std::size_t max_packages = 4096
);

}  // namespace hp2
