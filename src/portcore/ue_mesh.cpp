#include "hp2/ue_mesh.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace hp2 {
namespace {

constexpr std::int32_t kMaxVertices = 20'000'000;
constexpr std::int32_t kMaxTriangles = 5'000'000;
constexpr std::int32_t kMaxMeshEntries = 40'000'000;
constexpr std::int32_t kMaxMaterials = 65'536;
// G5 renders stored reference-pose vertices in Unreal world units. The private
// Duel10 probe found valid actor scales from 0.75 to 2.5, while a malformed
// inherited class-default path produced 200.0 and expanded human meshes past
// 20,000 units. Keep direct actor overrides untouched and reject only extreme
// inherited defaults until full UClass default-object serialization replaces
// the bounded tail scanner.
constexpr float kMaxTrustedInheritedDrawScale = 16.0f;

std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

class PayloadReader {
public:
    PayloadReader(
        const std::vector<std::uint8_t>& bytes,
        std::int32_t serial_offset,
        std::size_t position = 0
    ) : bytes_(bytes), serial_offset_(serial_offset), position_(position) {
        if (position_ > bytes_.size()) {
            throw std::runtime_error("mesh native-data offset is outside its payload");
        }
    }

    std::size_t Tell() const { return position_; }
    std::int64_t TellAbsolute() const {
        return static_cast<std::int64_t>(serial_offset_) + static_cast<std::int64_t>(position_);
    }
    std::size_t Remaining() const { return bytes_.size() - position_; }

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

    std::int32_t I32() { return static_cast<std::int32_t>(U32()); }

    float F32() {
        const std::uint32_t bits = U32();
        float value = 0.0f;
        static_assert(sizeof(value) == sizeof(bits));
        std::memcpy(&value, &bits, sizeof(value));
        if (!std::isfinite(value)) {
            throw std::runtime_error("mesh contains a non-finite float");
        }
        return value;
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
        throw std::runtime_error("mesh export has no serialized payload");
    }
    std::ifstream input(package.summary.path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open mesh package payload");
    }
    input.seekg(entry.serial_offset, std::ios::beg);
    if (!input) {
        throw std::runtime_error("cannot seek to mesh package payload");
    }
    std::vector<std::uint8_t> payload(static_cast<std::size_t>(entry.serial_size));
    input.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    if (input.gcount() != static_cast<std::streamsize>(payload.size())) {
        throw std::runtime_error("short read while loading mesh package payload");
    }
    return payload;
}

std::int32_t BoundedCount(
    PayloadReader& reader,
    std::int32_t limit,
    const char* description
) {
    const std::int32_t count = reader.CompactIndex();
    if (count < 0 || count > limit) {
        throw std::runtime_error(std::string(description) + " count is unreasonable");
    }
    return count;
}

Vec3 ReadVec3(PayloadReader& reader) {
    return {reader.F32(), reader.F32(), reader.F32()};
}

void SkipPrimitiveBounds(PayloadReader& reader, std::uint16_t version) {
    ReadVec3(reader);
    ReadVec3(reader);
    reader.U8();
    ReadVec3(reader);
    if (version > 61) {
        reader.F32();
    }
}

void ValidateLazyEnd(PayloadReader& reader, std::uint32_t expected, const char* description) {
    if (reader.TellAbsolute() != static_cast<std::int64_t>(expected)) {
        throw std::runtime_error(std::string(description) + " lazy-array end does not match");
    }
}

std::int32_t SignExtend(std::uint32_t value, std::uint32_t bits) {
    const std::uint32_t sign = 1u << (bits - 1u);
    return static_cast<std::int32_t>((value ^ sign) - sign);
}

struct LegacyTriangle {
    std::array<std::uint16_t, 3> indices{};
    std::array<std::array<std::uint8_t, 2>, 3> uv{};
    std::int32_t texture_index = 0;
};

struct LodFace {
    std::array<std::uint16_t, 3> wedges{};
    std::uint16_t material = 0;
};

struct LodWedge {
    std::uint16_t vertex = 0;
    std::uint8_t u = 0;
    std::uint8_t v = 0;
};

struct LodMaterial {
    std::int32_t texture_index = 0;
};

struct ObjectTarget {
    bool valid = false;
    bool embedded = false;
    std::string package_name;
    std::string object_name;
    std::string class_name;
    std::vector<std::string> groups;
    std::string error;
};

ObjectTarget ResolveObjectTarget(const PackageIndex& package, std::int32_t reference) {
    ObjectTarget result;
    if (reference == 0) {
        result.error = "object reference is null";
        return result;
    }
    if (reference > 0) {
        const std::size_t index = static_cast<std::size_t>(reference - 1);
        if (index >= package.exports.size()) {
            result.error = "object export reference is outside the table";
            return result;
        }
        const ExportEntry& entry = package.exports[index];
        result.embedded = true;
        result.package_name = package.summary.path.stem().string();
        result.object_name = entry.object_name;
        result.class_name = entry.class_name;
        result.valid = true;
        return result;
    }

    const std::int64_t import_index = -static_cast<std::int64_t>(reference) - 1;
    if (import_index < 0 || static_cast<std::size_t>(import_index) >= package.imports.size()) {
        result.error = "object import reference is outside the table";
        return result;
    }
    const ImportEntry& leaf = package.imports[static_cast<std::size_t>(import_index)];
    result.object_name = leaf.object_name;
    result.class_name = leaf.class_name;
    std::int32_t outer = leaf.package_index;
    for (std::size_t depth = 0; outer != 0 && depth < 64; ++depth) {
        if (outer < 0) {
            const std::int64_t index = -static_cast<std::int64_t>(outer) - 1;
            if (index < 0 || static_cast<std::size_t>(index) >= package.imports.size()) {
                result.error = "object outer import is outside the table";
                return result;
            }
            const ImportEntry& entry = package.imports[static_cast<std::size_t>(index)];
            if (Lowercase(entry.class_name) == "package" && entry.package_index == 0) {
                result.package_name = entry.object_name;
            } else {
                result.groups.push_back(entry.object_name);
            }
            outer = entry.package_index;
        } else {
            const std::size_t index = static_cast<std::size_t>(outer - 1);
            if (index >= package.exports.size()) {
                result.error = "object outer export is outside the table";
                return result;
            }
            result.groups.push_back(package.exports[index].object_name);
            outer = package.exports[index].package_index;
        }
    }
    if (outer != 0) {
        result.error = "object outer chain exceeds the depth limit";
        return result;
    }
    if (result.package_name.empty()) {
        result.error = "object reference has no root package";
        return result;
    }
    result.valid = true;
    return result;
}

std::vector<std::string> ExportGroups(const PackageIndex& package, const ExportEntry& entry) {
    std::vector<std::string> groups;
    std::int32_t outer = entry.package_index;
    for (std::size_t depth = 0; outer > 0 && depth < 64; ++depth) {
        const std::size_t index = static_cast<std::size_t>(outer - 1);
        if (index >= package.exports.size()) {
            return {};
        }
        groups.push_back(package.exports[index].object_name);
        outer = package.exports[index].package_index;
    }
    return groups;
}

std::size_t FindObjectExport(
    const PackageIndex& package,
    const ObjectTarget& target,
    bool mesh
) {
    std::size_t fallback = std::numeric_limits<std::size_t>::max();
    std::vector<std::string> target_groups = target.groups;
    for (auto& group : target_groups) {
        group = Lowercase(group);
    }
    for (std::size_t index = 0; index < package.exports.size(); ++index) {
        const ExportEntry& entry = package.exports[index];
        if (entry.serial_size <= 0
            || Lowercase(entry.object_name) != Lowercase(target.object_name)) {
            continue;
        }
        const std::string class_name = Lowercase(entry.class_name);
        const bool class_matches = mesh
            ? (class_name == "mesh" || class_name == "lodmesh" || class_name == "skeletalmesh")
            : class_name.find("texture") != std::string::npos;
        if (!class_matches) {
            continue;
        }
        if (fallback == std::numeric_limits<std::size_t>::max()) {
            fallback = index;
        }
        std::vector<std::string> groups = ExportGroups(package, entry);
        for (auto& group : groups) {
            group = Lowercase(group);
        }
        if (groups == target_groups) {
            return index;
        }
    }
    return fallback;
}

std::size_t FindClassExport(
    const PackageIndex& package,
    const ObjectTarget& target
) {
    std::size_t fallback = std::numeric_limits<std::size_t>::max();
    std::vector<std::string> target_groups = target.groups;
    for (auto& group : target_groups) {
        group = Lowercase(group);
    }
    for (std::size_t index = 0; index < package.exports.size(); ++index) {
        const ExportEntry& entry = package.exports[index];
        if (Lowercase(entry.object_name) != Lowercase(target.object_name)
            || Lowercase(entry.class_name) != "class") {
            continue;
        }
        if (fallback == std::numeric_limits<std::size_t>::max()) {
            fallback = index;
        }
        std::vector<std::string> groups = ExportGroups(package, entry);
        for (auto& group : groups) {
            group = Lowercase(group);
        }
        if (groups == target_groups) {
            return index;
        }
    }
    return fallback;
}

struct ClassPropertySource {
    SerializedProperty property;
    const PackageIndex* package = nullptr;
};

struct ResolvedClassDefaults {
    bool resolved = false;
    std::unordered_map<std::string, ClassPropertySource> properties;
};

std::unordered_map<std::string, std::filesystem::path> CatalogPackages(
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
            && IsPackageExtension(iterator->path())) {
            result.emplace(Lowercase(iterator->path().stem().string()), iterator->path());
        }
        iterator.increment(error_code);
    }
    return result;
}

Vec3 RotateUnreal(Vec3 point, const Rotator& rotation) {
    constexpr double kTurn = 6.28318530717958647692 / 65536.0;
    const double pitch = static_cast<double>(rotation.pitch) * kTurn;
    const double yaw = static_cast<double>(rotation.yaw) * kTurn;
    const double roll = static_cast<double>(rotation.roll) * kTurn;
    const double cr = std::cos(roll);
    const double sr = std::sin(roll);
    double x = point.x;
    double y = static_cast<double>(point.y) * cr - static_cast<double>(point.z) * sr;
    double z = static_cast<double>(point.y) * sr + static_cast<double>(point.z) * cr;
    const double cp = std::cos(pitch);
    const double sp = std::sin(pitch);
    const double pitched_x = x * cp + z * sp;
    z = -x * sp + z * cp;
    x = pitched_x;
    const double cy = std::cos(yaw);
    const double sy = std::sin(yaw);
    return {
        static_cast<float>(x * cy - y * sy),
        static_cast<float>(x * sy + y * cy),
        static_cast<float>(z)
    };
}

Vec3 TransformActorVertex(
    const Vec3& vertex,
    const DecodedVertexMesh& mesh,
    const ActorInstance& actor
) {
    Vec3 point = {
        (vertex.x - mesh.origin.x) * mesh.scale.x,
        (vertex.y - mesh.origin.y) * mesh.scale.y,
        (vertex.z - mesh.origin.z) * mesh.scale.z
    };
    point = RotateUnreal(point, mesh.rotation_origin);
    const Vec3 pivot = actor.has_pre_pivot ? actor.pre_pivot : Vec3{};
    const Vec3 scale_3d = actor.has_draw_scale_3d
        ? actor.draw_scale_3d : Vec3{1.0f, 1.0f, 1.0f};
    const float scale = actor.has_draw_scale ? actor.draw_scale : 1.0f;
    point = {
        (point.x - pivot.x) * scale * scale_3d.x,
        (point.y - pivot.y) * scale * scale_3d.y,
        (point.z - pivot.z) * scale * scale_3d.z
    };
    point = RotateUnreal(point, actor.has_rotation ? actor.rotation : Rotator{});
    return {
        point.x + actor.location.x,
        point.y + actor.location.y,
        point.z + actor.location.z
    };
}

}  // namespace

Vec3 TransformActorMeshVertex(
    const Vec3& vertex,
    const DecodedVertexMesh& mesh,
    const ActorInstance& actor
) {
    return TransformActorVertex(vertex, mesh, actor);
}

DecodedVertexMesh LoadVertexMeshExport(
    const PackageIndex& package,
    std::size_t export_index
) {
    DecodedVertexMesh result;
    result.package_path = package.summary.path;
    result.package_name = package.summary.path.stem().string();
    if (!package.valid) {
        result.error = package.error.empty() ? "mesh package index is invalid" : package.error;
        return result;
    }
    if (package.summary.file_version < 62 || package.summary.file_version >= 100) {
        result.error = "vertex-mesh reader currently supports UE1 versions 62-99";
        return result;
    }
    if (export_index >= package.exports.size()) {
        result.error = "mesh export index is outside the export table";
        return result;
    }
    const ExportEntry& entry = package.exports[export_index];
    result.object_name = entry.object_name;
    result.class_name = entry.class_name;
    const std::string mesh_class = Lowercase(entry.class_name);
    if (mesh_class != "mesh" && mesh_class != "lodmesh" && mesh_class != "skeletalmesh") {
        result.error = "selected export is not a Mesh, LodMesh or SkeletalMesh";
        return result;
    }

    try {
        const ObjectProperties properties = LoadObjectProperties(package, export_index);
        if (!properties.valid) {
            throw std::runtime_error(properties.error);
        }
        const std::vector<std::uint8_t> payload = ReadExportPayload(package, entry);
        PayloadReader reader(payload, entry.serial_offset, properties.native_data_offset);
        const std::uint16_t version = package.summary.file_version;
        SkipPrimitiveBounds(reader, version);

        const std::uint32_t vertices_end = reader.U32();
        const std::int32_t vertex_count = BoundedCount(reader, kMaxVertices, "Mesh.Verts");
        std::vector<Vec3> packed_vertices;
        packed_vertices.reserve(static_cast<std::size_t>(vertex_count));
        for (std::int32_t index = 0; index < vertex_count; ++index) {
            const std::uint32_t packed = reader.U32();
            packed_vertices.push_back({
                static_cast<float>(SignExtend(packed & 0x7ffu, 11)),
                static_cast<float>(SignExtend((packed >> 11u) & 0x7ffu, 11)),
                static_cast<float>(SignExtend((packed >> 22u) & 0x3ffu, 10))
            });
        }
        ValidateLazyEnd(reader, vertices_end, "Mesh.Verts");

        const std::uint32_t triangles_end = reader.U32();
        const std::int32_t triangle_count = BoundedCount(reader, kMaxTriangles, "Mesh.Tris");
        std::vector<LegacyTriangle> legacy_triangles;
        legacy_triangles.reserve(static_cast<std::size_t>(triangle_count));
        for (std::int32_t index = 0; index < triangle_count; ++index) {
            LegacyTriangle triangle;
            for (auto& vertex : triangle.indices) {
                vertex = reader.U16();
            }
            for (auto& uv : triangle.uv) {
                uv = {reader.U8(), reader.U8()};
            }
            reader.U32();
            triangle.texture_index = reader.I32();
            legacy_triangles.push_back(triangle);
        }
        ValidateLazyEnd(reader, triangles_end, "Mesh.Tris");

        const std::int32_t animation_sequence_count = BoundedCount(
            reader, kMaxMaterials, "Mesh.AnimSeqs"
        );
        for (std::int32_t index = 0; index < animation_sequence_count; ++index) {
            reader.CompactIndex();
            reader.CompactIndex();
            reader.I32();
            reader.I32();
            const std::int32_t notify_count = BoundedCount(
                reader, kMaxMeshEntries, "Mesh.Notifys"
            );
            for (std::int32_t notify = 0; notify < notify_count; ++notify) {
                reader.F32();
                reader.CompactIndex();
            }
            reader.F32();
        }

        const std::uint32_t connects_end = reader.U32();
        const std::int32_t connect_count = BoundedCount(reader, kMaxMeshEntries, "Mesh.Connects");
        reader.Skip(static_cast<std::size_t>(connect_count) * 8u, "Mesh.Connects");
        ValidateLazyEnd(reader, connects_end, "Mesh.Connects");
        SkipPrimitiveBounds(reader, version);

        const std::uint32_t links_end = reader.U32();
        const std::int32_t link_count = BoundedCount(reader, kMaxMeshEntries, "Mesh.VertLinks");
        reader.Skip(static_cast<std::size_t>(link_count) * 4u, "Mesh.VertLinks");
        ValidateLazyEnd(reader, links_end, "Mesh.VertLinks");

        const std::int32_t texture_count = BoundedCount(reader, kMaxMaterials, "Mesh.Textures");
        result.texture_references.reserve(static_cast<std::size_t>(texture_count));
        for (std::int32_t index = 0; index < texture_count; ++index) {
            result.texture_references.push_back(reader.CompactIndex());
        }
        const std::int32_t box_count = BoundedCount(reader, kMaxMeshEntries, "Mesh.BoundingBoxes");
        reader.Skip(static_cast<std::size_t>(box_count) * 25u, "Mesh.BoundingBoxes");
        const std::int32_t sphere_count = BoundedCount(reader, kMaxMeshEntries, "Mesh.BoundingSpheres");
        reader.Skip(static_cast<std::size_t>(sphere_count) * 16u, "Mesh.BoundingSpheres");

        result.frame_vertices = reader.I32();
        result.animation_frames = reader.I32();
        reader.U32();
        reader.U32();
        result.scale = ReadVec3(reader);
        result.origin = ReadVec3(reader);
        result.rotation_origin = {reader.I32(), reader.I32(), reader.I32()};
        reader.U32();
        reader.U32();
        if (version == 65) {
            reader.F32();
        } else if (version >= 66) {
            const std::int32_t lod_count = BoundedCount(reader, kMaxMaterials, "Mesh.TextureLOD");
            reader.Skip(static_cast<std::size_t>(lod_count) * 4u, "Mesh.TextureLOD");
        }

        std::vector<LodFace> faces;
        std::vector<LodWedge> wedges;
        std::vector<LodMaterial> materials;
        std::vector<std::uint16_t> remap;
        std::uint32_t special_vertices = 0;
        if (mesh_class == "lodmesh" || mesh_class == "skeletalmesh") {
            const std::int32_t collapse_points = BoundedCount(
                reader, kMaxMeshEntries, "LodMesh.CollapsePointThus"
            );
            reader.Skip(static_cast<std::size_t>(collapse_points) * 2u, "LodMesh.CollapsePointThus");
            const std::int32_t face_levels = BoundedCount(reader, kMaxTriangles, "LodMesh.FaceLevel");
            reader.Skip(static_cast<std::size_t>(face_levels) * 2u, "LodMesh.FaceLevel");
            const std::int32_t face_count = BoundedCount(reader, kMaxTriangles, "LodMesh.Faces");
            faces.reserve(static_cast<std::size_t>(face_count));
            for (std::int32_t index = 0; index < face_count; ++index) {
                LodFace face;
                face.wedges = {reader.U16(), reader.U16(), reader.U16()};
                face.material = reader.U16();
                faces.push_back(face);
            }
            const std::int32_t collapse_wedges = BoundedCount(
                reader, kMaxMeshEntries, "LodMesh.CollapseWedgeThus"
            );
            reader.Skip(static_cast<std::size_t>(collapse_wedges) * 2u, "LodMesh.CollapseWedgeThus");
            const std::int32_t wedge_count = BoundedCount(reader, kMaxMeshEntries, "LodMesh.Wedges");
            wedges.reserve(static_cast<std::size_t>(wedge_count));
            for (std::int32_t index = 0; index < wedge_count; ++index) {
                wedges.push_back({reader.U16(), reader.U8(), reader.U8()});
            }
            const std::int32_t material_count = BoundedCount(
                reader, kMaxMaterials, "LodMesh.Materials"
            );
            materials.reserve(static_cast<std::size_t>(material_count));
            for (std::int32_t index = 0; index < material_count; ++index) {
                reader.U32();
                materials.push_back({reader.I32()});
            }
            const std::int32_t special_faces = BoundedCount(
                reader, kMaxTriangles, "LodMesh.SpecialFaces"
            );
            reader.Skip(static_cast<std::size_t>(special_faces) * 8u, "LodMesh.SpecialFaces");
            reader.U32();
            special_vertices = reader.U32();
            reader.F32();
            reader.F32();
            reader.F32();
            reader.U32();
            reader.F32();
            reader.F32();
            const std::int32_t remap_count = BoundedCount(
                reader, kMaxMeshEntries, "LodMesh.ReMapAnimVerts"
            );
            remap.reserve(static_cast<std::size_t>(remap_count));
            for (std::int32_t index = 0; index < remap_count; ++index) {
                remap.push_back(reader.U16());
            }
            reader.U32();
        }

        std::vector<Vec3> reference_vertices = packed_vertices;
        if (mesh_class == "skeletalmesh") {
            const std::int32_t ext_wedge_count = BoundedCount(
                reader, kMaxMeshEntries, "SkeletalMesh.ExtWedges"
            );
            reader.Skip(static_cast<std::size_t>(ext_wedge_count) * 12u, "SkeletalMesh.ExtWedges");
            const std::int32_t point_count = BoundedCount(
                reader, kMaxVertices, "SkeletalMesh.Points"
            );
            reference_vertices.clear();
            reference_vertices.reserve(static_cast<std::size_t>(point_count));
            for (std::int32_t index = 0; index < point_count; ++index) {
                reference_vertices.push_back(ReadVec3(reader));
            }
            result.skeletal_points = reference_vertices.size();
            const std::int32_t bone_count = BoundedCount(
                reader, kMaxMaterials, "SkeletalMesh.RefSkeleton"
            );
            result.skeletal_bones = static_cast<std::size_t>(bone_count);
            for (std::int32_t index = 0; index < bone_count; ++index) {
                reader.CompactIndex();
                reader.U32();
                reader.Skip(16u, "skeletal bone orientation");
                reader.Skip(12u, "skeletal bone position");
                reader.F32();
                reader.Skip(12u, "skeletal bone size");
                reader.U32();
                reader.U32();
            }
            const std::int32_t weight_index_count = BoundedCount(
                reader, kMaxMeshEntries, "SkeletalMesh.BoneWeightIndices"
            );
            reader.Skip(static_cast<std::size_t>(weight_index_count) * 8u,
                        "SkeletalMesh.BoneWeightIndices");
            const std::int32_t weight_count = BoundedCount(
                reader, kMaxMeshEntries, "SkeletalMesh.BoneWeights"
            );
            reader.Skip(static_cast<std::size_t>(weight_count) * 4u, "SkeletalMesh.BoneWeights");
            const std::int32_t local_point_count = BoundedCount(
                reader, kMaxMeshEntries, "SkeletalMesh.LocalPoints"
            );
            reader.Skip(static_cast<std::size_t>(local_point_count) * 12u,
                        "SkeletalMesh.LocalPoints");
            reader.U32();
            reader.CompactIndex();
            reader.U32();
            reader.Skip(48u, "skeletal mesh weapon coordinates");
        }

        if (reference_vertices.empty()) {
            throw std::runtime_error("mesh contains no reference-pose vertices");
        }
        result.vertices = std::move(reference_vertices);
        if (mesh_class == "mesh") {
            for (const auto& triangle : legacy_triangles) {
                if (*std::max_element(triangle.indices.begin(), triangle.indices.end())
                    >= result.vertices.size()) {
                    continue;
                }
                VertexMeshTriangle decoded;
                decoded.indices = {triangle.indices[0], triangle.indices[1], triangle.indices[2]};
                decoded.texture_slot = triangle.texture_index;
                for (std::size_t corner = 0; corner < 3; ++corner) {
                    decoded.texture_coordinates[corner] = {
                        static_cast<float>(triangle.uv[corner][0]) / 255.0f,
                        static_cast<float>(triangle.uv[corner][1]) / 255.0f
                    };
                }
                result.triangles.push_back(decoded);
            }
        } else {
            for (const LodFace& face : faces) {
                if (*std::max_element(face.wedges.begin(), face.wedges.end()) >= wedges.size()) {
                    continue;
                }
                VertexMeshTriangle decoded;
                bool valid = true;
                for (std::size_t corner = 0; corner < 3; ++corner) {
                    const LodWedge& wedge = wedges[face.wedges[corner]];
                    std::uint32_t vertex = wedge.vertex;
                    if (mesh_class != "skeletalmesh") {
                        vertex += special_vertices;
                        if (!remap.empty()) {
                            if (vertex >= remap.size()) {
                                valid = false;
                                break;
                            }
                            vertex = remap[vertex];
                        }
                    }
                    if (vertex >= result.vertices.size()) {
                        valid = false;
                        break;
                    }
                    decoded.indices[corner] = vertex;
                    decoded.texture_coordinates[corner] = {
                        static_cast<float>(wedge.u) / 255.0f,
                        static_cast<float>(wedge.v) / 255.0f
                    };
                }
                if (!valid) {
                    continue;
                }
                decoded.texture_slot = face.material < materials.size()
                    ? materials[face.material].texture_index : 0;
                result.triangles.push_back(decoded);
            }
        }
        if (result.triangles.empty()) {
            throw std::runtime_error("mesh contains no valid reference-pose triangles");
        }
        result.valid = true;
    } catch (const std::exception& exception) {
        result.error = exception.what();
        result.vertices.clear();
        result.triangles.clear();
        result.texture_references.clear();
    }
    return result;
}

ActorMeshScene LoadDirectActorMeshes(
    const std::filesystem::path& game_root,
    const PackageIndex& map_package,
    const LevelActorCensus& actors,
    std::size_t max_triangles
) {
    ActorMeshScene result;
    if (!map_package.valid) {
        result.error = map_package.error.empty() ? "map package is invalid" : map_package.error;
        return result;
    }
    if (!actors.valid) {
        result.error = actors.error.empty() ? "actor census is invalid" : actors.error;
        return result;
    }
    const auto catalog = CatalogPackages(game_root);
    std::unordered_map<std::string, PackageIndex> packages;
    std::unordered_map<std::string, DecodedVertexMesh> meshes;
    std::unordered_map<std::string, std::int32_t> materials;
    std::unordered_map<std::string, ResolvedClassDefaults> class_defaults;
    std::unordered_map<std::int32_t, ResolvedClassDefaults> actor_class_defaults;
    std::string last_error = "map has no renderable direct or inherited actor mesh";

    auto target_package = [&](const PackageIndex& source, const ObjectTarget& target)
        -> const PackageIndex* {
        if (target.embedded) {
            return &source;
        }
        const auto path = catalog.find(Lowercase(target.package_name));
        if (path == catalog.end()) {
            return nullptr;
        }
        const std::string key = path->second.generic_string();
        auto found = packages.find(key);
        if (found == packages.end()) {
            found = packages.emplace(key, LoadPackageIndex(path->second)).first;
        }
        return &found->second;
    };

    std::function<ResolvedClassDefaults(
        const PackageIndex&, std::size_t, std::unordered_set<std::string>&
    )> resolve_class_defaults;
    resolve_class_defaults = [&](const PackageIndex& class_package,
                                 std::size_t class_export_index,
                                 std::unordered_set<std::string>& visiting)
        -> ResolvedClassDefaults {
        const std::string key = class_package.summary.path.generic_string() + "#"
            + std::to_string(class_export_index);
        const auto cached = class_defaults.find(key);
        if (cached != class_defaults.end()) {
            return cached->second;
        }

        ResolvedClassDefaults resolved;
        if (class_export_index >= class_package.exports.size()
            || !visiting.insert(key).second) {
            ++result.class_resolution_failures;
            return resolved;
        }
        const ExportEntry& entry = class_package.exports[class_export_index];
        if (Lowercase(entry.class_name) != "class") {
            ++result.class_resolution_failures;
            visiting.erase(key);
            return resolved;
        }

        if (entry.super_index != 0) {
            const ObjectTarget parent_target = ResolveObjectTarget(
                class_package, entry.super_index
            );
            if (parent_target.valid) {
                const PackageIndex* parent_package = target_package(
                    class_package, parent_target
                );
                if (parent_package != nullptr && parent_package->valid) {
                    const std::size_t parent_export = FindClassExport(
                        *parent_package, parent_target
                    );
                    if (parent_export != std::numeric_limits<std::size_t>::max()) {
                        resolved = resolve_class_defaults(
                            *parent_package, parent_export, visiting
                        );
                    } else {
                        ++result.class_resolution_failures;
                    }
                } else {
                    ++result.class_resolution_failures;
                }
            } else {
                ++result.class_resolution_failures;
            }
        }

        if (entry.serial_size > 0) {
            ++result.class_exports_scanned;
            const ObjectProperties local = ScanClassDefaultProperties(
                class_package, class_export_index
            );
            if (local.valid) {
                ++result.class_default_streams_found;
                for (const SerializedProperty& property : local.properties) {
                    const std::string property_name = Lowercase(property.name);
                    if (property_name == "mesh" || property_name == "staticmesh"
                        || property_name == "prepivot" || property_name == "drawscale"
                        || property_name == "drawscale3d" || property_name == "bhidden"
                        || property_name == "skin" || property_name == "multiskins") {
                        resolved.properties[property_name] = {property, &class_package};
                    }
                }
            } else {
                ++result.class_default_scan_misses;
            }
        }
        resolved.resolved = true;
        visiting.erase(key);
        class_defaults.emplace(key, resolved);
        return resolved;
    };

    auto defaults_for_actor = [&](const ActorInstance& actor) -> ResolvedClassDefaults {
        const auto cached = actor_class_defaults.find(actor.class_reference);
        if (cached != actor_class_defaults.end()) {
            return cached->second;
        }
        ResolvedClassDefaults resolved;
        if (actor.class_reference != 0) {
            const ObjectTarget target = ResolveObjectTarget(
                map_package, actor.class_reference
            );
            if (target.valid) {
                const PackageIndex* package = target_package(map_package, target);
                if (package != nullptr && package->valid) {
                    const std::size_t export_index = FindClassExport(*package, target);
                    if (export_index != std::numeric_limits<std::size_t>::max()) {
                        std::unordered_set<std::string> visiting;
                        resolved = resolve_class_defaults(*package, export_index, visiting);
                    } else {
                        ++result.class_resolution_failures;
                    }
                } else {
                    ++result.class_resolution_failures;
                }
            } else {
                ++result.class_resolution_failures;
            }
        }
        actor_class_defaults.emplace(actor.class_reference, resolved);
        return resolved;
    };

    auto material_for = [&](const PackageIndex& source, std::int32_t reference) -> std::int32_t {
        if (reference == 0) {
            return -1;
        }
        const ObjectTarget target = ResolveObjectTarget(source, reference);
        if (!target.valid) {
            ++result.failed_materials;
            last_error = target.error;
            return -1;
        }
        const PackageIndex* package = target_package(source, target);
        if (package == nullptr || !package->valid) {
            ++result.failed_materials;
            last_error = "actor texture package was not found or is invalid";
            return -1;
        }
        const std::size_t export_index = FindObjectExport(*package, target, false);
        if (export_index == std::numeric_limits<std::size_t>::max()) {
            ++result.failed_materials;
            last_error = "actor texture export was not found";
            return -1;
        }
        const std::string key = package->summary.path.generic_string() + "#"
            + std::to_string(export_index);
        const auto cached = materials.find(key);
        if (cached != materials.end()) {
            return cached->second;
        }
        DecodedTexture texture = LoadTextureExport(*package, export_index);
        if (!texture.valid) {
            ++result.failed_materials;
            last_error = texture.error;
            return -1;
        }
        const std::int32_t material = static_cast<std::int32_t>(result.materials.size());
        result.materials.push_back({reference, std::move(texture)});
        materials.emplace(key, material);
        result.decoded_materials = result.materials.size();
        return material;
    };

    for (const ActorInstance& actor : actors.actors) {
        if (result.triangles.size() >= max_triangles) {
            break;
        }
        ActorInstance effective_actor = actor;
        bool inherited_draw_scale = false;
        bool rejected_inherited_draw_scale = false;
        float source_draw_scale = effective_actor.draw_scale;
        const ResolvedClassDefaults defaults = defaults_for_actor(actor);
        auto default_property = [&](const char* name) -> const ClassPropertySource* {
            const auto found = defaults.properties.find(name);
            return found == defaults.properties.end() ? nullptr : &found->second;
        };

        if (!effective_actor.has_pre_pivot) {
            const ClassPropertySource* value = default_property("prepivot");
            effective_actor.has_pre_pivot = value != nullptr
                && DecodePropertyVec3(&value->property, effective_actor.pre_pivot);
        }
        if (!effective_actor.has_draw_scale) {
            const ClassPropertySource* value = default_property("drawscale");
            effective_actor.has_draw_scale = value != nullptr
                && DecodePropertyFloat(&value->property, effective_actor.draw_scale);
            inherited_draw_scale = effective_actor.has_draw_scale;
            source_draw_scale = effective_actor.draw_scale;
            if (inherited_draw_scale
                && std::abs(effective_actor.draw_scale) > kMaxTrustedInheritedDrawScale) {
                effective_actor.has_draw_scale = false;
                effective_actor.draw_scale = 1.0f;
                rejected_inherited_draw_scale = true;
                ++result.rejected_inherited_draw_scales;
            }
        }
        if (!effective_actor.has_draw_scale_3d) {
            const ClassPropertySource* value = default_property("drawscale3d");
            effective_actor.has_draw_scale_3d = value != nullptr
                && DecodePropertyVec3(&value->property, effective_actor.draw_scale_3d);
        }
        if (!effective_actor.has_hidden) {
            const ClassPropertySource* value = default_property("bhidden");
            effective_actor.has_hidden = value != nullptr
                && DecodePropertyBool(&value->property, effective_actor.hidden);
        }

        std::int32_t mesh_reference = actor.mesh_reference;
        const PackageIndex* mesh_reference_package = &map_package;
        bool inherited_mesh = false;
        if (mesh_reference == 0) {
            const ClassPropertySource* value = default_property("mesh");
            if (value != nullptr) {
                mesh_reference = DecodePropertyObjectReference(&value->property);
                mesh_reference_package = value->package;
                inherited_mesh = mesh_reference != 0;
            }
        }
        if (mesh_reference == 0 || mesh_reference_package == nullptr) {
            continue;
        }
        ++result.candidate_instances;
        result.direct_mesh_candidates += inherited_mesh ? 0u : 1u;
        result.inherited_mesh_candidates += inherited_mesh ? 1u : 0u;
        const ObjectTarget target = ResolveObjectTarget(
            *mesh_reference_package, mesh_reference
        );
        if (!target.valid) {
            ++result.failed_mesh_instances;
            last_error = target.error;
            continue;
        }
        const PackageIndex* mesh_package = target_package(*mesh_reference_package, target);
        if (mesh_package == nullptr || !mesh_package->valid) {
            ++result.failed_mesh_instances;
            last_error = "actor mesh package was not found or is invalid";
            continue;
        }
        const std::size_t export_index = FindObjectExport(*mesh_package, target, true);
        if (export_index == std::numeric_limits<std::size_t>::max()) {
            ++result.failed_mesh_instances;
            last_error = "actor mesh export was not found";
            continue;
        }
        const std::string mesh_key = mesh_package->summary.path.generic_string() + "#"
            + std::to_string(export_index);
        auto mesh_entry = meshes.find(mesh_key);
        bool inserted_mesh = false;
        if (mesh_entry == meshes.end()) {
            mesh_entry = meshes.emplace(
                mesh_key, LoadVertexMeshExport(*mesh_package, export_index)
            ).first;
            inserted_mesh = true;
        }
        const DecodedVertexMesh& mesh = mesh_entry->second;
        if (!mesh.valid) {
            ++result.failed_mesh_instances;
            last_error = mesh.error;
            continue;
        }
        if (inserted_mesh) {
            ActorMeshAssetSummary asset;
            asset.package_name = mesh.package_name;
            asset.object_name = mesh.object_name;
            asset.class_name = mesh.class_name;
            asset.vertices = mesh.vertices.size();
            asset.triangles = mesh.triangles.size();
            asset.texture_slots = mesh.texture_references.size();
            asset.skeletal_points = mesh.skeletal_points;
            asset.skeletal_bones = mesh.skeletal_bones;
            asset.mesh_scale = mesh.scale;
            asset.mesh_origin = mesh.origin;
            for (const Vec3& vertex : mesh.vertices) {
                if (!asset.vertex_bounds_valid) {
                    asset.vertex_bounds_min = vertex;
                    asset.vertex_bounds_max = vertex;
                    asset.vertex_bounds_valid = true;
                } else {
                    asset.vertex_bounds_min.x = std::min(asset.vertex_bounds_min.x, vertex.x);
                    asset.vertex_bounds_min.y = std::min(asset.vertex_bounds_min.y, vertex.y);
                    asset.vertex_bounds_min.z = std::min(asset.vertex_bounds_min.z, vertex.z);
                    asset.vertex_bounds_max.x = std::max(asset.vertex_bounds_max.x, vertex.x);
                    asset.vertex_bounds_max.y = std::max(asset.vertex_bounds_max.y, vertex.y);
                    asset.vertex_bounds_max.z = std::max(asset.vertex_bounds_max.z, vertex.z);
                }
            }
            result.assets.push_back(std::move(asset));
            result.decoded_mesh_assets = result.assets.size();
        }

        const std::string actor_class = Lowercase(actor.class_name);
        if (effective_actor.hidden || actor_class == "camera") {
            continue;
        }
        std::vector<std::int32_t> slot_materials(mesh.texture_references.size(), -1);
        for (std::size_t slot = 0; slot < mesh.texture_references.size(); ++slot) {
            slot_materials[slot] = material_for(*mesh_package, mesh.texture_references[slot]);
        }

        ++result.decoded_mesh_instances;
        result.decoded_inherited_mesh_instances += inherited_mesh ? 1u : 0u;
        ActorMeshInstanceSummary instance;
        instance.actor_object_name = actor.object_name;
        instance.actor_class_name = actor.class_name;
        instance.mesh_package_name = mesh.package_name;
        instance.mesh_object_name = mesh.object_name;
        instance.inherited_mesh = inherited_mesh;
        instance.has_location = effective_actor.has_location;
        instance.location = effective_actor.location;
        instance.has_pre_pivot = effective_actor.has_pre_pivot;
        instance.pre_pivot = effective_actor.pre_pivot;
        instance.has_draw_scale = effective_actor.has_draw_scale;
        instance.draw_scale = effective_actor.draw_scale;
        instance.inherited_draw_scale = inherited_draw_scale;
        instance.source_draw_scale = source_draw_scale;
        instance.rejected_inherited_draw_scale = rejected_inherited_draw_scale;
        instance.has_draw_scale_3d = effective_actor.has_draw_scale_3d;
        instance.draw_scale_3d = effective_actor.draw_scale_3d;
        instance.source_triangles = mesh.triangles.size();
        const std::size_t first_placed_triangle = result.triangles.size();
        for (const VertexMeshTriangle& triangle : mesh.triangles) {
            if (result.triangles.size() >= max_triangles) {
                break;
            }
            ActorMeshTriangle placed;
            for (std::size_t corner = 0; corner < 3; ++corner) {
                placed.points[corner] = TransformActorMeshVertex(
                    mesh.vertices[triangle.indices[corner]], mesh, effective_actor
                );
                placed.source_point_indices[corner] = triangle.indices[corner];
                placed.texture_coordinates[corner] = triangle.texture_coordinates[corner];
                const Vec3& point = placed.points[corner];
                if (!instance.bounds_valid) {
                    instance.bounds_min = point;
                    instance.bounds_max = point;
                    instance.bounds_valid = true;
                } else {
                    instance.bounds_min.x = std::min(instance.bounds_min.x, point.x);
                    instance.bounds_min.y = std::min(instance.bounds_min.y, point.y);
                    instance.bounds_min.z = std::min(instance.bounds_min.z, point.z);
                    instance.bounds_max.x = std::max(instance.bounds_max.x, point.x);
                    instance.bounds_max.y = std::max(instance.bounds_max.y, point.y);
                    instance.bounds_max.z = std::max(instance.bounds_max.z, point.z);
                }
                if (!result.bounds_valid) {
                    result.bounds_min = point;
                    result.bounds_max = point;
                    result.bounds_valid = true;
                } else {
                    result.bounds_min.x = std::min(result.bounds_min.x, point.x);
                    result.bounds_min.y = std::min(result.bounds_min.y, point.y);
                    result.bounds_min.z = std::min(result.bounds_min.z, point.z);
                    result.bounds_max.x = std::max(result.bounds_max.x, point.x);
                    result.bounds_max.y = std::max(result.bounds_max.y, point.y);
                    result.bounds_max.z = std::max(result.bounds_max.z, point.z);
                }
            }
            if (triangle.texture_slot >= 0
                && static_cast<std::size_t>(triangle.texture_slot) < slot_materials.size()) {
                placed.material_index = slot_materials[static_cast<std::size_t>(triangle.texture_slot)];
            }
            result.textured_triangles += placed.material_index >= 0 ? 1u : 0u;
            result.triangles.push_back(placed);
            ++instance.emitted_triangles;
        }
        if (!result.animation_source.valid && instance.emitted_triangles > 0u
            && Lowercase(actor.object_name) == "duellist0"
            && Lowercase(mesh.class_name) == "skeletalmesh") {
            result.animation_source.valid = true;
            result.animation_source.first_triangle = first_placed_triangle;
            result.animation_source.triangle_count = instance.emitted_triangles;
            result.animation_source.mesh = mesh;
            result.animation_source.actor = effective_actor;
        }
        result.instances.push_back(std::move(instance));
    }
    result.source_triangles = result.triangles.size();
    result.valid = !result.triangles.empty();
    if (!result.valid) {
        result.error = last_error;
    }
    return result;
}

}  // namespace hp2
