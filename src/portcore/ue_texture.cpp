#include "hp2/ue_texture.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace hp2 {
namespace {

constexpr std::int32_t kMaxMipCount = 64;
constexpr std::int32_t kMaxTextureDimension = 16384;
constexpr std::int32_t kMaxPaletteColors = 4096;
constexpr std::size_t kMaxTextureBytes = 256u * 1024u * 1024u;

std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

class PayloadReader {
public:
    PayloadReader(const std::vector<std::uint8_t>& bytes, std::int32_t serial_offset)
        : bytes_(bytes), serial_offset_(serial_offset) {}

    std::size_t Tell() const {
        return position_;
    }

    std::int64_t TellAbsolute() const {
        return static_cast<std::int64_t>(serial_offset_) + static_cast<std::int64_t>(position_);
    }

    std::size_t Remaining() const {
        return bytes_.size() - position_;
    }

    void Skip(std::size_t count, const char* description) {
        Require(count, description);
        position_ += count;
    }

    std::uint8_t U8() {
        Require(1, "byte");
        return bytes_[position_++];
    }

    std::uint16_t U16() {
        const std::uint16_t low = U8();
        return static_cast<std::uint16_t>(low | (static_cast<std::uint16_t>(U8()) << 8u));
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
        const bool negative = (first & 0x80u) != 0u;
        std::uint32_t value = first & 0x3fu;
        bool more = (first & 0x40u) != 0u;
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
            more = (byte & 0x80u) != 0u;
        }
        if (value > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
            throw std::runtime_error("compact index is outside signed range");
        }
        const auto signed_value = static_cast<std::int32_t>(value);
        return negative ? -signed_value : signed_value;
    }

    std::vector<std::uint8_t> Bytes(std::size_t count, const char* description) {
        Require(count, description);
        std::vector<std::uint8_t> result(
            bytes_.begin() + static_cast<std::ptrdiff_t>(position_),
            bytes_.begin() + static_cast<std::ptrdiff_t>(position_ + count)
        );
        position_ += count;
        return result;
    }

private:
    void Require(std::size_t count, const char* description) const {
        if (count > bytes_.size() - position_) {
            throw std::runtime_error(std::string("unexpected end while reading ") + description);
        }
    }

    const std::vector<std::uint8_t>& bytes_;
    std::int32_t serial_offset_ = 0;
    std::size_t position_ = 0;
};

std::vector<std::uint8_t> ReadExportPayload(
    const PackageIndex& package,
    const ExportEntry& entry
) {
    if (entry.serial_size <= 0 || entry.serial_offset < 0) {
        throw std::runtime_error("export has no serialized payload");
    }
    std::ifstream input(package.summary.path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open texture package payload");
    }
    input.seekg(entry.serial_offset, std::ios::beg);
    if (!input) {
        throw std::runtime_error("cannot seek to texture package payload");
    }
    std::vector<std::uint8_t> payload(static_cast<std::size_t>(entry.serial_size));
    input.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    if (input.gcount() != static_cast<std::streamsize>(payload.size())) {
        throw std::runtime_error("short read while loading texture package payload");
    }
    return payload;
}

void SkipArrayIndex(PayloadReader& reader) {
    const std::uint8_t first = reader.U8();
    if (first < 128u) {
        return;
    }
    const std::uint8_t second = reader.U8();
    const std::uint16_t short_value = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(second) << 8u) | first
    ) & 0x7fffu;
    if (short_value < 16384u) {
        return;
    }
    reader.Skip(2, "property array index");
}

std::size_t TaggedPropertySize(PayloadReader& reader, std::uint8_t info) {
    switch ((info & 0x70u) >> 4u) {
        case 0: return 1;
        case 1: return 2;
        case 2: return 4;
        case 3: return 12;
        case 4: return 16;
        case 5: return reader.U8();
        case 6: return reader.U16();
        case 7: return reader.U32();
        default: throw std::runtime_error("invalid property size code");
    }
}

struct TextureProperties {
    std::int32_t palette_reference = 0;
    bool has_palette_reference = false;
};

TextureProperties ReadTaggedProperties(PayloadReader& reader, const PackageIndex& package) {
    TextureProperties result;
    for (std::size_t property_index = 0; property_index < 65536; ++property_index) {
        const std::int32_t name_index = reader.CompactIndex();
        if (name_index < 0 || static_cast<std::size_t>(name_index) >= package.names.size()) {
            throw std::runtime_error("property name index is outside the package name table");
        }
        const std::string property_name = Lowercase(
            package.names[static_cast<std::size_t>(name_index)].value
        );
        if (property_name == "none") {
            return result;
        }

        const std::uint8_t info = reader.U8();
        const std::uint8_t type = info & 0x0fu;
        if (type == 0u) {
            throw std::runtime_error("invalid tagged property type");
        }
        if (type == 10u) {
            const std::int32_t struct_name = reader.CompactIndex();
            if (struct_name < 0 || static_cast<std::size_t>(struct_name) >= package.names.size()) {
                throw std::runtime_error("property struct name is outside the package name table");
            }
        }
        const std::size_t serialized_size = TaggedPropertySize(reader, info);
        if ((info & 0x80u) != 0u && type != 3u) {
            SkipArrayIndex(reader);
        }

        switch (type) {
            case 1u:
                reader.U8();
                break;
            case 2u:
            case 4u:
                reader.U32();
                break;
            case 3u:
                break;
            case 5u:
            case 8u: {
                const std::int32_t reference = reader.CompactIndex();
                if (property_name == "palette" && type == 5u) {
                    result.palette_reference = reference;
                    result.has_palette_reference = true;
                }
                break;
            }
            case 6u:
                reader.CompactIndex();
                break;
            default:
                reader.Skip(serialized_size, "tagged property payload");
                break;
        }
    }
    throw std::runtime_error("tagged property list has no None terminator");
}

std::vector<std::array<std::uint8_t, 4>> LoadPalette(
    const PackageIndex& package,
    std::int32_t palette_reference,
    std::string& palette_name
) {
    if (palette_reference <= 0) {
        throw std::runtime_error("texture palette is not an export in its package");
    }
    const std::size_t export_index = static_cast<std::size_t>(palette_reference - 1);
    if (export_index >= package.exports.size()) {
        throw std::runtime_error("texture palette export reference is outside the export table");
    }
    const ExportEntry& entry = package.exports[export_index];
    if (Lowercase(entry.class_name) != "palette") {
        throw std::runtime_error("texture Palette property does not reference a Palette export");
    }
    palette_name = entry.object_name;
    const std::vector<std::uint8_t> payload = ReadExportPayload(package, entry);
    PayloadReader reader(payload, entry.serial_offset);
    ReadTaggedProperties(reader, package);
    const std::int32_t color_count = reader.CompactIndex();
    if (color_count <= 0 || color_count > kMaxPaletteColors) {
        throw std::runtime_error("palette color count is unreasonable");
    }
    std::vector<std::array<std::uint8_t, 4>> colors;
    colors.reserve(static_cast<std::size_t>(color_count));
    for (std::int32_t color_index = 0; color_index < color_count; ++color_index) {
        colors.push_back({reader.U8(), reader.U8(), reader.U8(), reader.U8()});
    }
    return colors;
}

struct MaterialTarget {
    std::string package_name;
    std::string object_name;
    std::string class_name;
    std::vector<std::string> groups;
    bool embedded = false;
    bool valid = false;
    std::string error;
};

MaterialTarget ResolveMaterialTarget(const PackageIndex& package, std::int32_t reference) {
    MaterialTarget result;
    if (reference == 0) {
        result.error = "surface has no material";
        return result;
    }
    if (reference > 0) {
        const std::size_t export_index = static_cast<std::size_t>(reference - 1);
        if (export_index >= package.exports.size()) {
            result.error = "surface material export reference is outside the table";
            return result;
        }
        const ExportEntry& entry = package.exports[export_index];
        result.package_name = package.summary.path.stem().string();
        result.object_name = entry.object_name;
        result.class_name = entry.class_name;
        result.embedded = true;
        result.valid = true;
        return result;
    }

    const std::int64_t import_number = -static_cast<std::int64_t>(reference) - 1;
    if (import_number < 0 || static_cast<std::size_t>(import_number) >= package.imports.size()) {
        result.error = "surface material import reference is outside the table";
        return result;
    }
    const ImportEntry& material = package.imports[static_cast<std::size_t>(import_number)];
    result.object_name = material.object_name;
    result.class_name = material.class_name;

    std::int32_t outer_reference = material.package_index;
    for (std::size_t depth = 0; outer_reference != 0 && depth < 64; ++depth) {
        if (outer_reference < 0) {
            const std::int64_t outer_number = -static_cast<std::int64_t>(outer_reference) - 1;
            if (outer_number < 0 || static_cast<std::size_t>(outer_number) >= package.imports.size()) {
                result.error = "surface material outer import is outside the table";
                return result;
            }
            const ImportEntry& outer = package.imports[static_cast<std::size_t>(outer_number)];
            if (Lowercase(outer.class_name) == "package") {
                result.package_name = outer.object_name;
            } else {
                result.groups.push_back(outer.object_name);
            }
            outer_reference = outer.package_index;
        } else {
            const std::size_t outer_number = static_cast<std::size_t>(outer_reference - 1);
            if (outer_number >= package.exports.size()) {
                result.error = "surface material outer export is outside the table";
                return result;
            }
            result.groups.push_back(package.exports[outer_number].object_name);
            outer_reference = package.exports[outer_number].package_index;
        }
    }
    if (outer_reference != 0) {
        result.error = "surface material outer chain is too deep";
        return result;
    }
    if (result.package_name.empty()) {
        result.error = "surface material has no owning package import";
        return result;
    }
    result.valid = true;
    return result;
}

std::vector<std::string> ResolveExportGroups(
    const PackageIndex& package,
    const ExportEntry& entry
) {
    std::vector<std::string> groups;
    std::int32_t outer_reference = entry.package_index;
    for (std::size_t depth = 0; outer_reference > 0 && depth < 64; ++depth) {
        const std::size_t outer_number = static_cast<std::size_t>(outer_reference - 1);
        if (outer_number >= package.exports.size()) {
            groups.clear();
            return groups;
        }
        groups.push_back(package.exports[outer_number].object_name);
        outer_reference = package.exports[outer_number].package_index;
    }
    return groups;
}

std::size_t FindTextureExport(const PackageIndex& package, const MaterialTarget& target) {
    std::size_t fallback = std::numeric_limits<std::size_t>::max();
    const std::string target_name = Lowercase(target.object_name);
    std::vector<std::string> target_groups = target.groups;
    for (std::string& group : target_groups) {
        group = Lowercase(group);
    }
    for (std::size_t index = 0; index < package.exports.size(); ++index) {
        const ExportEntry& entry = package.exports[index];
        if (entry.serial_size <= 0 || Lowercase(entry.object_name) != target_name) {
            continue;
        }
        const std::string class_name = Lowercase(entry.class_name);
        if (class_name.find("texture") == std::string::npos) {
            continue;
        }
        if (fallback == std::numeric_limits<std::size_t>::max()) {
            fallback = index;
        }
        std::vector<std::string> groups = ResolveExportGroups(package, entry);
        for (std::string& group : groups) {
            group = Lowercase(group);
        }
        if (groups == target_groups) {
            return index;
        }
    }
    return fallback;
}

std::unordered_map<std::string, std::filesystem::path> CatalogTexturePackages(
    const std::filesystem::path& root
) {
    std::unordered_map<std::string, std::filesystem::path> result;
    std::error_code error_code;
    if (!std::filesystem::is_directory(root, error_code)) {
        return result;
    }
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    std::filesystem::recursive_directory_iterator iterator(root, options, error_code);
    const std::filesystem::recursive_directory_iterator end;
    while (iterator != end) {
        if (error_code) {
            error_code.clear();
            iterator.increment(error_code);
            continue;
        }
        if (iterator->is_regular_file(error_code) && !error_code
            && Lowercase(iterator->path().extension().string()) == ".utx") {
            result.emplace(Lowercase(iterator->path().stem().string()), iterator->path());
        }
        iterator.increment(error_code);
    }
    return result;
}

}  // namespace

bool ComputeSurfaceTextureCoordinate(
    const ModelGeometry& geometry,
    std::int32_t surface_index,
    std::uint32_t point_index,
    std::int32_t texture_width,
    std::int32_t texture_height,
    TextureCoordinate& coordinate
) {
    if (surface_index < 0 || static_cast<std::size_t>(surface_index) >= geometry.surfaces.size()
        || point_index >= geometry.points.size() || texture_width <= 0 || texture_height <= 0) {
        return false;
    }
    const BspSurface& surface = geometry.surfaces[static_cast<std::size_t>(surface_index)];
    if (surface.base_point_index < 0
        || static_cast<std::size_t>(surface.base_point_index) >= geometry.points.size()
        || surface.texture_u_vector_index < 0
        || static_cast<std::size_t>(surface.texture_u_vector_index) >= geometry.vectors.size()
        || surface.texture_v_vector_index < 0
        || static_cast<std::size_t>(surface.texture_v_vector_index) >= geometry.vectors.size()) {
        return false;
    }
    const Vec3& point = geometry.points[point_index];
    const Vec3& base = geometry.points[static_cast<std::size_t>(surface.base_point_index)];
    const Vec3& texture_u = geometry.vectors[static_cast<std::size_t>(surface.texture_u_vector_index)];
    const Vec3& texture_v = geometry.vectors[static_cast<std::size_t>(surface.texture_v_vector_index)];
    const double distance_x = static_cast<double>(point.x) - base.x;
    const double distance_y = static_cast<double>(point.y) - base.y;
    const double distance_z = static_cast<double>(point.z) - base.z;
    const double u = (
        distance_x * texture_u.x + distance_y * texture_u.y + distance_z * texture_u.z
        + static_cast<double>(surface.pan_u)
    ) / static_cast<double>(texture_width);
    const double v = (
        distance_x * texture_v.x + distance_y * texture_v.y + distance_z * texture_v.z
        + static_cast<double>(surface.pan_v)
    ) / static_cast<double>(texture_height);
    if (!std::isfinite(u) || !std::isfinite(v)
        || std::abs(u) > static_cast<double>(std::numeric_limits<float>::max())
        || std::abs(v) > static_cast<double>(std::numeric_limits<float>::max())) {
        return false;
    }
    coordinate.u = static_cast<float>(u);
    coordinate.v = static_cast<float>(v);
    return true;
}

DecodedTexture LoadTextureExport(const PackageIndex& package, std::size_t export_index) {
    DecodedTexture result;
    result.package_path = package.summary.path;
    result.package_name = package.summary.path.stem().string();
    if (!package.valid) {
        result.error = package.error.empty() ? "texture package index is invalid" : package.error;
        return result;
    }
    if (package.summary.file_version < 64 || package.summary.file_version >= 100) {
        result.error = "texture reader currently supports UE1 package versions 64-99";
        return result;
    }
    if (export_index >= package.exports.size()) {
        result.error = "texture export index is outside the export table";
        return result;
    }
    const ExportEntry& entry = package.exports[export_index];
    result.object_name = entry.object_name;
    if (Lowercase(entry.class_name).find("texture") == std::string::npos) {
        result.error = "selected export is not a Texture object";
        return result;
    }

    try {
        const std::vector<std::uint8_t> payload = ReadExportPayload(package, entry);
        PayloadReader reader(payload, entry.serial_offset);
        const TextureProperties properties = ReadTaggedProperties(reader, package);
        if (!properties.has_palette_reference || properties.palette_reference == 0) {
            throw std::runtime_error("P8 texture has no Palette object property");
        }

        const std::int32_t mip_count = reader.CompactIndex();
        if (mip_count <= 0 || mip_count > kMaxMipCount) {
            throw std::runtime_error("texture mip count is unreasonable");
        }
        std::vector<std::uint8_t> first_indices;
        std::int32_t first_width = 0;
        std::int32_t first_height = 0;
        for (std::int32_t mip_index = 0; mip_index < mip_count; ++mip_index) {
            const std::int32_t lazy_end = reader.I32();
            const std::int32_t byte_count = reader.CompactIndex();
            if (byte_count < 0 || static_cast<std::size_t>(byte_count) > kMaxTextureBytes) {
                throw std::runtime_error("texture mip byte count is unreasonable");
            }
            std::vector<std::uint8_t> indices = reader.Bytes(
                static_cast<std::size_t>(byte_count), "texture mip pixels"
            );
            if (lazy_end != reader.TellAbsolute()) {
                throw std::runtime_error("texture lazy-array end offset does not match its payload");
            }
            const std::int32_t width = reader.I32();
            const std::int32_t height = reader.I32();
            const std::uint8_t u_bits = reader.U8();
            const std::uint8_t v_bits = reader.U8();
            if (width <= 0 || height <= 0 || width > kMaxTextureDimension
                || height > kMaxTextureDimension || u_bits > 30u || v_bits > 30u) {
                throw std::runtime_error("texture mip dimensions are unreasonable");
            }
            if ((1u << u_bits) != static_cast<std::uint32_t>(width)
                || (1u << v_bits) != static_cast<std::uint32_t>(height)) {
                throw std::runtime_error("texture mip dimensions do not match their bit counts");
            }
            if (mip_index == 0) {
                first_indices = std::move(indices);
                first_width = width;
                first_height = height;
            }
        }
        const std::uint64_t pixel_count = static_cast<std::uint64_t>(first_width)
            * static_cast<std::uint64_t>(first_height);
        if (pixel_count != first_indices.size()) {
            throw std::runtime_error("texture first mip is not an uncompressed P8 image");
        }
        std::vector<std::array<std::uint8_t, 4>> palette = LoadPalette(
            package, properties.palette_reference, result.palette_name
        );
        result.rgba_pixels.reserve(static_cast<std::size_t>(pixel_count) * 4u);
        for (const std::uint8_t palette_index : first_indices) {
            if (palette_index >= palette.size()) {
                throw std::runtime_error("texture pixel references a missing palette color");
            }
            const auto& color = palette[palette_index];
            result.rgba_pixels.push_back(color[0]);
            result.rgba_pixels.push_back(color[1]);
            result.rgba_pixels.push_back(color[2]);
            result.rgba_pixels.push_back(255u);
        }
        result.width = first_width;
        result.height = first_height;
        result.valid = true;
    } catch (const std::exception& exception) {
        result.error = exception.what();
        result.rgba_pixels.clear();
    }
    return result;
}

DecodedTexture LoadFirstSurfaceTexture(
    const std::filesystem::path& game_root,
    const PackageIndex& map_package,
    const ModelGeometry& geometry
) {
    DecodedTexture result;
    if (!map_package.valid) {
        result.error = map_package.error.empty() ? "map package index is invalid" : map_package.error;
        return result;
    }
    if (!geometry.valid) {
        result.error = geometry.error.empty() ? "map geometry is invalid" : geometry.error;
        return result;
    }

    std::map<std::int32_t, std::size_t> triangle_counts;
    for (const BspTriangle& triangle : geometry.triangles) {
        if (triangle.surface_index < 0
            || static_cast<std::size_t>(triangle.surface_index) >= geometry.surfaces.size()) {
            continue;
        }
        ++triangle_counts[geometry.surfaces[static_cast<std::size_t>(triangle.surface_index)].material_index];
    }
    std::vector<std::pair<std::int32_t, std::size_t>> candidates(
        triangle_counts.begin(), triangle_counts.end()
    );
    std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
        return left.second > right.second;
    });

    const auto package_catalog = CatalogTexturePackages(game_root);
    std::unordered_map<std::string, PackageIndex> loaded_packages;
    std::string last_error = "map has no textured BSP surfaces";
    for (const auto& candidate : candidates) {
        const MaterialTarget target = ResolveMaterialTarget(map_package, candidate.first);
        if (!target.valid) {
            last_error = target.error;
            continue;
        }
        const PackageIndex* texture_package = nullptr;
        if (target.embedded) {
            texture_package = &map_package;
        } else {
            const auto path_iterator = package_catalog.find(Lowercase(target.package_name));
            if (path_iterator == package_catalog.end()) {
                last_error = "texture package " + target.package_name + " was not found";
                continue;
            }
            const std::string cache_key = path_iterator->second.generic_string();
            auto loaded = loaded_packages.find(cache_key);
            if (loaded == loaded_packages.end()) {
                loaded = loaded_packages.emplace(cache_key, LoadPackageIndex(path_iterator->second)).first;
            }
            texture_package = &loaded->second;
        }
        if (!texture_package->valid) {
            last_error = texture_package->error;
            continue;
        }
        const std::size_t export_index = FindTextureExport(*texture_package, target);
        if (export_index == std::numeric_limits<std::size_t>::max()) {
            last_error = "texture export " + target.object_name + " was not found in "
                + target.package_name;
            continue;
        }
        DecodedTexture decoded = LoadTextureExport(*texture_package, export_index);
        if (!decoded.valid) {
            last_error = decoded.error;
            continue;
        }
        decoded.map_material_index = candidate.first;
        decoded.triangle_count = candidate.second;
        return decoded;
    }
    result.error = last_error;
    return result;
}

DecodedTexture LoadFirstSurfaceTexture(
    const std::filesystem::path& game_root,
    const std::filesystem::path& map_path,
    const ModelGeometry& geometry
) {
    const PackageIndex package = LoadPackageIndex(map_path);
    return LoadFirstSurfaceTexture(game_root, package, geometry);
}

}  // namespace hp2
