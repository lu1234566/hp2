#include "hp2/ue_package.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace hp2 {
namespace {

constexpr std::size_t kSummaryBytes = 36;
constexpr std::int32_t kMaxReasonableEntries = 2'000'000;
constexpr std::int32_t kMaxNameBytes = 4096;

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

class PackageReader {
public:
    PackageReader(const std::filesystem::path& path, std::uintmax_t file_size)
        : input_(path, std::ios::binary), file_size_(file_size) {
        if (!input_) {
            throw std::runtime_error("cannot open package index");
        }
    }

    void Seek(std::int32_t offset, const char* table_name) {
        if (offset < 0 || static_cast<std::uintmax_t>(offset) > file_size_) {
            throw std::runtime_error(std::string(table_name) + " offset is outside the package");
        }
        input_.clear();
        input_.seekg(offset, std::ios::beg);
        if (!input_) {
            throw std::runtime_error(std::string("cannot seek to ") + table_name);
        }
    }

    std::uint8_t U8() {
        char byte = 0;
        input_.read(&byte, 1);
        if (!input_) {
            throw std::runtime_error("unexpected end of package");
        }
        return static_cast<std::uint8_t>(byte);
    }

    std::uint16_t U16() {
        const std::uint16_t a = U8();
        const std::uint16_t b = U8();
        return static_cast<std::uint16_t>(a | (b << 8u));
    }

    std::uint32_t U32() {
        std::uint32_t value = 0;
        for (std::uint32_t shift = 0; shift < 32; shift += 8) {
            value |= static_cast<std::uint32_t>(U8()) << shift;
        }
        return value;
    }

    std::int32_t I32() {
        return static_cast<std::int32_t>(U32());
    }

    std::int32_t CompactIndex() {
        const std::uint8_t first = U8();
        const bool negative = (first & 0x80u) != 0;
        std::uint32_t value = first & 0x3fu;
        bool more = (first & 0x40u) != 0;
        std::uint32_t shift = 6;
        for (int byte_index = 1; more; ++byte_index) {
            if (byte_index >= 5 || shift >= 32) {
                throw std::runtime_error("compact index exceeds 32 bits");
            }
            const std::uint8_t byte = U8();
            const std::uint32_t payload = byte & 0x7fu;
            if (shift == 27 && payload > 0x0fu) {
                throw std::runtime_error("compact index overflows 32 bits");
            }
            value |= payload << shift;
            shift += 7;
            more = (byte & 0x80u) != 0;
        }
        if (value > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
            throw std::runtime_error("compact index is outside signed range");
        }
        const auto signed_value = static_cast<std::int32_t>(value);
        return negative ? -signed_value : signed_value;
    }

    std::string NameString(std::uint16_t file_version) {
        if (file_version < 64) {
            std::string value;
            for (std::int32_t index = 0; index < kMaxNameBytes; ++index) {
                const char character = static_cast<char>(U8());
                if (character == '\0') {
                    return value;
                }
                value.push_back(character);
            }
            throw std::runtime_error("name is not null terminated");
        }

        const std::int32_t serialized_length = CompactIndex();
        if (serialized_length == 0) {
            return {};
        }
        if (serialized_length > kMaxNameBytes || serialized_length < -kMaxNameBytes) {
            throw std::runtime_error("name length is unreasonable");
        }

        std::string value;
        if (serialized_length > 0) {
            value.reserve(static_cast<std::size_t>(serialized_length - 1));
            for (std::int32_t index = 0; index < serialized_length; ++index) {
                const char character = static_cast<char>(U8());
                if (index + 1 == serialized_length) {
                    if (character != '\0') {
                        throw std::runtime_error("ANSI name is not null terminated");
                    }
                } else {
                    value.push_back(character);
                }
            }
            return value;
        }

        const std::int32_t code_unit_count = -serialized_length;
        value.reserve(static_cast<std::size_t>(code_unit_count - 1));
        for (std::int32_t index = 0; index < code_unit_count; ++index) {
            const std::uint16_t code_unit = U16();
            if (index + 1 == code_unit_count) {
                if (code_unit != 0) {
                    throw std::runtime_error("Unicode name is not null terminated");
                }
            } else if (code_unit <= 0x7fu) {
                value.push_back(static_cast<char>(code_unit));
            } else {
                value.push_back('?');
            }
        }
        return value;
    }

private:
    std::ifstream input_;
    std::uintmax_t file_size_ = 0;
};

std::string ResolveName(const std::vector<NameEntry>& names, std::int32_t index) {
    if (index < 0 || static_cast<std::size_t>(index) >= names.size()) {
        throw std::runtime_error("name index is outside the name table");
    }
    return names[static_cast<std::size_t>(index)].value;
}

std::string ResolveReferenceName(const PackageIndex& package, std::int32_t package_index) {
    if (package_index < 0) {
        const std::int64_t import_index = -static_cast<std::int64_t>(package_index) - 1;
        if (import_index >= 0 && static_cast<std::size_t>(import_index) < package.imports.size()) {
            return package.imports[static_cast<std::size_t>(import_index)].object_name;
        }
    } else if (package_index > 0) {
        const std::int64_t export_index = static_cast<std::int64_t>(package_index) - 1;
        if (export_index >= 0 && static_cast<std::size_t>(export_index) < package.exports.size()) {
            return package.exports[static_cast<std::size_t>(export_index)].object_name;
        }
    }
    return package_index == 0 ? "Class" : "<invalid-reference>";
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

PackageIndex LoadPackageIndex(const std::filesystem::path& path) {
    PackageIndex result;
    result.summary = ProbePackage(path);
    if (!result.summary.valid) {
        result.error = result.summary.error;
        return result;
    }

    try {
        PackageReader reader(path, result.summary.file_size);

        reader.Seek(result.summary.name_offset, "name table");
        result.names.reserve(static_cast<std::size_t>(result.summary.name_count));
        for (std::int32_t index = 0; index < result.summary.name_count; ++index) {
            NameEntry entry;
            entry.value = reader.NameString(result.summary.file_version);
            entry.flags = reader.U32();
            result.names.push_back(std::move(entry));
        }

        reader.Seek(result.summary.import_offset, "import table");
        result.imports.reserve(static_cast<std::size_t>(result.summary.import_count));
        for (std::int32_t index = 0; index < result.summary.import_count; ++index) {
            ImportEntry entry;
            entry.class_package_name_index = reader.CompactIndex();
            entry.class_name_index = reader.CompactIndex();
            entry.package_index = reader.I32();
            entry.object_name_index = reader.CompactIndex();
            entry.class_package = ResolveName(result.names, entry.class_package_name_index);
            entry.class_name = ResolveName(result.names, entry.class_name_index);
            entry.object_name = ResolveName(result.names, entry.object_name_index);
            result.imports.push_back(std::move(entry));
        }

        reader.Seek(result.summary.export_offset, "export table");
        result.exports.reserve(static_cast<std::size_t>(result.summary.export_count));
        for (std::int32_t index = 0; index < result.summary.export_count; ++index) {
            ExportEntry entry;
            entry.class_index = reader.CompactIndex();
            entry.super_index = reader.CompactIndex();
            entry.package_index = reader.I32();
            entry.object_name_index = reader.CompactIndex();
            entry.object_flags = reader.U32();
            entry.serial_size = reader.CompactIndex();
            if (entry.serial_size < 0) {
                throw std::runtime_error("export serial size is negative");
            }
            if (entry.serial_size > 0) {
                entry.serial_offset = reader.CompactIndex();
                const std::uint64_t serial_end = static_cast<std::uint64_t>(entry.serial_offset)
                    + static_cast<std::uint64_t>(entry.serial_size);
                if (entry.serial_offset < 0 || serial_end > result.summary.file_size) {
                    throw std::runtime_error("export payload is outside the package");
                }
            }
            entry.object_name = ResolveName(result.names, entry.object_name_index);
            result.exports.push_back(std::move(entry));
        }

        for (auto& entry : result.exports) {
            entry.class_name = ResolveReferenceName(result, entry.class_index);
        }
        result.valid = true;
    } catch (const std::exception& exception) {
        result.error = exception.what();
        result.names.clear();
        result.imports.clear();
        result.exports.clear();
    }
    return result;
}

bool IsGeometryCandidate(const ExportEntry& entry) {
    const std::string class_name = Lowercase(entry.class_name);
    const std::string object_name = Lowercase(entry.object_name);
    return class_name == "model" || class_name == "polys" || class_name == "level"
        || object_name == "mylevel" || object_name == "model" || object_name == "polys";
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
