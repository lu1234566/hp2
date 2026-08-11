#include "hp2/ue_model.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace hp2 {
namespace {

constexpr std::uint32_t kObjectHasStack = 0x02000000u;
constexpr std::uint32_t kObjectNative = 0x04000000u;
constexpr std::int32_t kMaxArrayEntries = 2'000'000;

std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

class PayloadReader {
public:
    explicit PayloadReader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}

    std::size_t Tell() const {
        return position_;
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

    std::uint64_t U64() {
        std::uint64_t value = 0;
        for (std::uint32_t shift = 0; shift < 64; shift += 8) {
            value |= static_cast<std::uint64_t>(U8()) << shift;
        }
        return value;
    }

    float F32() {
        const std::uint32_t bits = U32();
        float value = 0.0f;
        static_assert(sizeof(value) == sizeof(bits));
        std::memcpy(&value, &bits, sizeof(value));
        if (!std::isfinite(value)) {
            throw std::runtime_error("model contains a non-finite float");
        }
        return value;
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

private:
    void Require(std::size_t count, const char* description) const {
        if (count > bytes_.size() - position_) {
            throw std::runtime_error(std::string("unexpected end while reading ") + description);
        }
    }

    const std::vector<std::uint8_t>& bytes_;
    std::size_t position_ = 0;
};

std::int32_t ArrayCount(PayloadReader& reader, const char* description) {
    const std::int32_t count = reader.CompactIndex();
    if (count < 0 || count > kMaxArrayEntries) {
        throw std::runtime_error(std::string(description) + " count is unreasonable");
    }
    return count;
}

Vec3 ReadVec3(PayloadReader& reader) {
    return {reader.F32(), reader.F32(), reader.F32()};
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

void SkipTaggedProperties(PayloadReader& reader, const PackageIndex& package) {
    for (std::size_t property_index = 0; property_index < 65536; ++property_index) {
        const std::int32_t name_index = reader.CompactIndex();
        if (name_index < 0 || static_cast<std::size_t>(name_index) >= package.names.size()) {
            throw std::runtime_error("property name index is outside the package name table");
        }
        if (Lowercase(package.names[static_cast<std::size_t>(name_index)].value) == "none") {
            return;
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
        if (type != 3u) {
            reader.Skip(serialized_size, "tagged property payload");
        }
    }
    throw std::runtime_error("tagged property list has no None terminator");
}

std::vector<std::uint8_t> ReadExportPayload(
    const PackageIndex& package,
    const ExportEntry& entry
) {
    std::ifstream input(package.summary.path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open map payload");
    }
    input.seekg(entry.serial_offset, std::ios::beg);
    if (!input) {
        throw std::runtime_error("cannot seek to model payload");
    }

    std::vector<std::uint8_t> payload(static_cast<std::size_t>(entry.serial_size));
    input.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    if (input.gcount() != static_cast<std::streamsize>(payload.size())) {
        throw std::runtime_error("short read while loading model payload");
    }
    return payload;
}

std::vector<Vec3> ReadVec3Array(PayloadReader& reader, const char* description) {
    const std::int32_t count = ArrayCount(reader, description);
    if (static_cast<std::size_t>(count) > reader.Remaining() / 12u) {
        throw std::runtime_error(std::string(description) + " exceeds the model payload");
    }
    std::vector<Vec3> values;
    values.reserve(static_cast<std::size_t>(count));
    for (std::int32_t index = 0; index < count; ++index) {
        values.push_back(ReadVec3(reader));
    }
    return values;
}

void UpdateBounds(ModelGeometry& geometry, const Vec3& point, bool& has_bounds) {
    if (!has_bounds) {
        geometry.bounds_min = point;
        geometry.bounds_max = point;
        has_bounds = true;
        return;
    }
    geometry.bounds_min.x = std::min(geometry.bounds_min.x, point.x);
    geometry.bounds_min.y = std::min(geometry.bounds_min.y, point.y);
    geometry.bounds_min.z = std::min(geometry.bounds_min.z, point.z);
    geometry.bounds_max.x = std::max(geometry.bounds_max.x, point.x);
    geometry.bounds_max.y = std::max(geometry.bounds_max.y, point.y);
    geometry.bounds_max.z = std::max(geometry.bounds_max.z, point.z);
}

bool IsNonDegenerate(const Vec3& a, const Vec3& b, const Vec3& c) {
    const double ab_x = static_cast<double>(b.x) - a.x;
    const double ab_y = static_cast<double>(b.y) - a.y;
    const double ab_z = static_cast<double>(b.z) - a.z;
    const double ac_x = static_cast<double>(c.x) - a.x;
    const double ac_y = static_cast<double>(c.y) - a.y;
    const double ac_z = static_cast<double>(c.z) - a.z;
    const double cross_x = ab_y * ac_z - ab_z * ac_y;
    const double cross_y = ab_z * ac_x - ab_x * ac_z;
    const double cross_z = ab_x * ac_y - ab_y * ac_x;
    const double magnitude_squared = cross_x * cross_x + cross_y * cross_y + cross_z * cross_z;
    return std::isfinite(magnitude_squared) && magnitude_squared > 1.0e-12;
}

void Triangulate(ModelGeometry& geometry) {
    bool has_bounds = false;
    for (std::size_t node_index = 0; node_index < geometry.nodes.size(); ++node_index) {
        const BspNode& node = geometry.nodes[node_index];
        if (node.vertex_count < 3u) {
            continue;
        }
        if (node.vertex_pool_index < 0 || node.surface_index < 0
            || static_cast<std::size_t>(node.surface_index) >= geometry.surfaces.size()) {
            ++geometry.skipped_nodes;
            continue;
        }
        const std::size_t first = static_cast<std::size_t>(node.vertex_pool_index);
        const std::size_t count = node.vertex_count;
        if (first > geometry.vertices.size() || count > geometry.vertices.size() - first) {
            ++geometry.skipped_nodes;
            continue;
        }

        bool points_valid = true;
        for (std::size_t offset = 0; offset < count; ++offset) {
            const std::int32_t point_index = geometry.vertices[first + offset].point_index;
            if (point_index < 0 || static_cast<std::size_t>(point_index) >= geometry.points.size()) {
                points_valid = false;
                break;
            }
        }
        if (!points_valid) {
            ++geometry.skipped_nodes;
            continue;
        }

        const auto root = static_cast<std::uint32_t>(geometry.vertices[first].point_index);
        for (std::size_t offset = 1; offset + 1 < count; ++offset) {
            const auto b = static_cast<std::uint32_t>(geometry.vertices[first + offset].point_index);
            const auto c = static_cast<std::uint32_t>(geometry.vertices[first + offset + 1].point_index);
            if (root == b || root == c || b == c
                || !IsNonDegenerate(geometry.points[root], geometry.points[b], geometry.points[c])) {
                ++geometry.skipped_triangles;
                continue;
            }
            geometry.triangles.push_back({
                root,
                b,
                c,
                static_cast<std::uint32_t>(node_index),
                node.surface_index
            });
            UpdateBounds(geometry, geometry.points[root], has_bounds);
            UpdateBounds(geometry, geometry.points[b], has_bounds);
            UpdateBounds(geometry, geometry.points[c], has_bounds);
        }
    }
    if (!has_bounds || geometry.triangles.empty()) {
        throw std::runtime_error("model did not yield any valid BSP triangles");
    }
}

}  // namespace

std::size_t FindLargestModelExport(const PackageIndex& package) {
    std::size_t selected = std::numeric_limits<std::size_t>::max();
    std::int32_t selected_size = -1;
    for (std::size_t index = 0; index < package.exports.size(); ++index) {
        const ExportEntry& entry = package.exports[index];
        if (Lowercase(entry.class_name) == "model" && entry.serial_size > selected_size) {
            selected = index;
            selected_size = entry.serial_size;
        }
    }
    return selected;
}

ModelGeometry LoadModelGeometry(const PackageIndex& package, std::size_t export_index) {
    ModelGeometry result;
    result.export_index = export_index;
    if (!package.valid) {
        result.error = package.error.empty() ? "package index is invalid" : package.error;
        return result;
    }
    if (package.summary.file_version < 68 || package.summary.file_version >= 100) {
        result.error = "model payload reader currently supports UE1 package versions 68-99";
        return result;
    }
    if (export_index >= package.exports.size()) {
        result.error = "model export index is outside the export table";
        return result;
    }

    const ExportEntry& entry = package.exports[export_index];
    result.object_name = entry.object_name;
    if (Lowercase(entry.class_name) != "model" || entry.serial_size <= 0) {
        result.error = "selected export is not a serialized Model object";
        return result;
    }
    if ((entry.object_flags & (kObjectHasStack | kObjectNative)) != 0u) {
        result.error = "stacked or native Model objects are not supported";
        return result;
    }

    try {
        const std::vector<std::uint8_t> payload = ReadExportPayload(package, entry);
        PayloadReader reader(payload);

        SkipTaggedProperties(reader, package);
        ReadVec3(reader);  // primitive bounding-box minimum
        ReadVec3(reader);  // primitive bounding-box maximum
        reader.U8();       // primitive bounding-box validity
        ReadVec3(reader);  // primitive bounding-sphere center
        reader.F32();      // primitive bounding-sphere radius

        result.vectors = ReadVec3Array(reader, "vector array");
        result.points = ReadVec3Array(reader, "point array");

        const std::int32_t node_count = ArrayCount(reader, "BSP node array");
        result.nodes.reserve(static_cast<std::size_t>(node_count));
        for (std::int32_t index = 0; index < node_count; ++index) {
            reader.F32();
            reader.F32();
            reader.F32();
            reader.F32();
            reader.U64();
            BspNode node;
            node.flags = reader.U8();
            node.vertex_pool_index = reader.CompactIndex();
            node.surface_index = reader.CompactIndex();
            node.back_index = reader.CompactIndex();
            node.front_index = reader.CompactIndex();
            node.plane_index = reader.CompactIndex();
            reader.CompactIndex();  // collision bound
            reader.CompactIndex();  // render bound
            reader.CompactIndex();  // back zone
            reader.CompactIndex();  // front zone
            node.vertex_count = reader.U8();
            reader.I32();           // back leaf
            reader.I32();           // front leaf
            if (node.vertex_count > 24u) {
                throw std::runtime_error("BSP node vertex count exceeds the UE1 limit");
            }
            result.nodes.push_back(node);
        }

        const std::int32_t surface_count = ArrayCount(reader, "BSP surface array");
        result.surfaces.reserve(static_cast<std::size_t>(surface_count));
        for (std::int32_t index = 0; index < surface_count; ++index) {
            BspSurface surface;
            surface.material_index = reader.CompactIndex();
            surface.poly_flags = reader.U32();
            surface.base_point_index = reader.CompactIndex();
            surface.normal_vector_index = reader.CompactIndex();
            surface.texture_u_vector_index = reader.CompactIndex();
            surface.texture_v_vector_index = reader.CompactIndex();
            reader.CompactIndex();  // light-map index
            reader.CompactIndex();  // source brush polygon
            surface.pan_u = static_cast<std::int16_t>(reader.U16());
            surface.pan_v = static_cast<std::int16_t>(reader.U16());
            reader.CompactIndex();  // source brush actor
            result.surfaces.push_back(surface);
        }

        const std::int32_t vertex_count = ArrayCount(reader, "BSP vertex array");
        result.vertices.reserve(static_cast<std::size_t>(vertex_count));
        for (std::int32_t index = 0; index < vertex_count; ++index) {
            result.vertices.push_back({reader.CompactIndex(), reader.CompactIndex()});
        }

        reader.I32();  // shared-side count
        const std::int32_t zone_count = reader.I32();
        if (zone_count < 0 || zone_count > 64) {
            throw std::runtime_error("model zone count exceeds the UE1 limit");
        }
        for (std::int32_t index = 0; index < zone_count; ++index) {
            reader.CompactIndex();
            reader.U64();
            reader.U64();
        }
        reader.CompactIndex();  // Polys object reference

        result.payload_bytes_consumed = reader.Tell();
        Triangulate(result);
        result.valid = true;
    } catch (const std::exception& exception) {
        result.error = exception.what();
        result.vectors.clear();
        result.points.clear();
        result.nodes.clear();
        result.surfaces.clear();
        result.vertices.clear();
        result.triangles.clear();
    }
    return result;
}

ModelGeometry LoadPrimaryModelGeometry(const std::filesystem::path& map_path) {
    const PackageIndex package = LoadPackageIndex(map_path);
    if (!package.valid) {
        ModelGeometry result;
        result.error = package.error;
        return result;
    }
    const std::size_t export_index = FindLargestModelExport(package);
    if (export_index == std::numeric_limits<std::size_t>::max()) {
        ModelGeometry result;
        result.error = "package contains no serialized Model export";
        return result;
    }
    return LoadModelGeometry(package, export_index);
}

}  // namespace hp2
