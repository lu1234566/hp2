#include "hp2/ue_skeletal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace hp2 {
namespace {

constexpr std::int32_t kMaxVertices = 20'000'000;
constexpr std::int32_t kMaxTriangles = 5'000'000;
constexpr std::int32_t kMaxEntries = 40'000'000;
constexpr std::int32_t kMaxMaterials = 65'536;

std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

class Reader {
public:
    Reader(const std::vector<std::uint8_t>& bytes, std::int32_t serial_offset, std::size_t position)
        : bytes_(bytes), serial_offset_(serial_offset), position_(position) {
        if (position_ > bytes_.size()) {
            throw std::runtime_error("skeletal native-data offset is outside its payload");
        }
    }

    std::size_t Tell() const { return position_; }
    std::size_t Remaining() const { return bytes_.size() - position_; }
    std::int64_t TellAbsolute() const {
        return static_cast<std::int64_t>(serial_offset_) + static_cast<std::int64_t>(position_);
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

    std::int32_t I32() { return static_cast<std::int32_t>(U32()); }

    float F32() {
        const std::uint32_t raw = U32();
        float value = 0.0f;
        static_assert(sizeof(value) == sizeof(raw));
        std::memcpy(&value, &raw, sizeof(value));
        if (!std::isfinite(value)) {
            throw std::runtime_error("skeletal stream contains a non-finite float");
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

std::vector<std::uint8_t> ReadPayload(const PackageIndex& package, const ExportEntry& entry) {
    if (entry.serial_size <= 0 || entry.serial_offset < 0) {
        throw std::runtime_error("skeletal mesh export has no serialized payload");
    }
    std::ifstream input(package.summary.path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open skeletal mesh package payload");
    }
    input.seekg(entry.serial_offset, std::ios::beg);
    if (!input) {
        throw std::runtime_error("cannot seek to skeletal mesh package payload");
    }
    std::vector<std::uint8_t> payload(static_cast<std::size_t>(entry.serial_size));
    input.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    if (input.gcount() != static_cast<std::streamsize>(payload.size())) {
        throw std::runtime_error("short read while loading skeletal mesh package payload");
    }
    return payload;
}

std::int32_t Count(Reader& reader, std::int32_t limit, const char* description) {
    const std::int32_t value = reader.CompactIndex();
    if (value < 0 || value > limit) {
        throw std::runtime_error(std::string(description) + " count is unreasonable");
    }
    return value;
}

void SkipBytesForCount(Reader& reader, std::int32_t count, std::size_t stride, const char* description) {
    if (count < 0 || static_cast<std::uint64_t>(count) * stride
            > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error(std::string(description) + " byte count overflows");
    }
    reader.Skip(static_cast<std::size_t>(count) * stride, description);
}

Vec3 ReadVec3(Reader& reader) {
    return {reader.F32(), reader.F32(), reader.F32()};
}

void SkipPrimitiveBounds(Reader& reader, std::uint16_t version) {
    ReadVec3(reader);
    ReadVec3(reader);
    reader.U8();
    ReadVec3(reader);
    if (version > 61) {
        reader.F32();
    }
}

void ValidateLazyEnd(Reader& reader, std::uint32_t expected, const char* description) {
    if (reader.TellAbsolute() != static_cast<std::int64_t>(expected)) {
        throw std::runtime_error(std::string(description) + " lazy-array end does not match");
    }
}

std::string NameAt(const PackageIndex& package, std::int32_t index) {
    if (index < 0 || static_cast<std::size_t>(index) >= package.names.size()) {
        return {};
    }
    return package.names[static_cast<std::size_t>(index)].value;
}

std::string ObjectNameAt(const PackageIndex& package, std::int32_t reference) {
    if (reference > 0 && static_cast<std::size_t>(reference) <= package.exports.size()) {
        return package.exports[static_cast<std::size_t>(reference - 1)].object_name;
    }
    if (reference < 0) {
        const std::int64_t import_index = -static_cast<std::int64_t>(reference) - 1;
        if (import_index >= 0
            && static_cast<std::size_t>(import_index) < package.imports.size()) {
            return package.imports[static_cast<std::size_t>(import_index)].object_name;
        }
    }
    return {};
}

std::filesystem::path FindPackage(
    const std::filesystem::path& root,
    const std::string& package_name
) {
    const std::string wanted = Lowercase(package_name);
    std::error_code error_code;
    if (!std::filesystem::is_directory(root, error_code)) {
        return {};
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
            && IsPackageExtension(iterator->path())
            && Lowercase(iterator->path().stem().string()) == wanted) {
            return iterator->path();
        }
        iterator.increment(error_code);
    }
    return {};
}

std::size_t FindSkeletalExport(const PackageIndex& package, const std::string& object_name) {
    const std::string wanted = Lowercase(object_name);
    for (std::size_t index = 0; index < package.exports.size(); ++index) {
        const ExportEntry& entry = package.exports[index];
        if (entry.serial_size > 0
            && Lowercase(entry.class_name) == "skeletalmesh"
            && Lowercase(entry.object_name) == wanted) {
            return index;
        }
    }
    return std::numeric_limits<std::size_t>::max();
}

}  // namespace

SkeletalMeshSkinningData LoadSkeletalMeshSkinningExport(
    const PackageIndex& package,
    std::size_t export_index
) {
    SkeletalMeshSkinningData result;
    result.package_path = package.summary.path;
    result.package_name = package.summary.path.stem().string();
    result.file_version = package.summary.file_version;

    if (!package.valid) {
        result.error = package.error.empty() ? "skeletal package index is invalid" : package.error;
        return result;
    }
    if (package.summary.file_version < 62 || package.summary.file_version >= 100) {
        result.error = "skeletal probe currently supports UE1 versions 62-99";
        return result;
    }
    if (export_index >= package.exports.size()) {
        result.error = "skeletal export index is outside the export table";
        return result;
    }
    const ExportEntry& entry = package.exports[export_index];
    result.object_name = entry.object_name;
    if (Lowercase(entry.class_name) != "skeletalmesh") {
        result.error = "selected export is not a SkeletalMesh";
        return result;
    }

    try {
        const ObjectProperties properties = LoadObjectProperties(package, export_index);
        if (!properties.valid) {
            throw std::runtime_error(properties.error);
        }
        const std::vector<std::uint8_t> payload = ReadPayload(package, entry);
        Reader reader(payload, entry.serial_offset, properties.native_data_offset);
        const std::uint16_t version = package.summary.file_version;

        SkipPrimitiveBounds(reader, version);

        const std::uint32_t vertices_end = reader.U32();
        const std::int32_t vertex_count = Count(reader, kMaxVertices, "Mesh.Verts");
        SkipBytesForCount(reader, vertex_count, 4u, "Mesh.Verts");
        ValidateLazyEnd(reader, vertices_end, "Mesh.Verts");

        const std::uint32_t triangles_end = reader.U32();
        const std::int32_t triangle_count = Count(reader, kMaxTriangles, "Mesh.Tris");
        SkipBytesForCount(reader, triangle_count, 20u, "Mesh.Tris");
        ValidateLazyEnd(reader, triangles_end, "Mesh.Tris");

        const std::int32_t sequence_count = Count(reader, kMaxMaterials, "Mesh.AnimSeqs");
        result.sequences.reserve(static_cast<std::size_t>(sequence_count));
        for (std::int32_t index = 0; index < sequence_count; ++index) {
            SkeletalAnimationSequence sequence;
            sequence.name = NameAt(package, reader.CompactIndex());
            sequence.group = NameAt(package, reader.CompactIndex());
            sequence.start_frame = reader.I32();
            sequence.frame_count = reader.I32();
            const std::int32_t notify_count = Count(reader, kMaxEntries, "Mesh.Notifys");
            sequence.notify_count = static_cast<std::size_t>(notify_count);
            for (std::int32_t notify = 0; notify < notify_count; ++notify) {
                reader.F32();
                reader.CompactIndex();
            }
            sequence.rate = reader.F32();
            result.sequences.push_back(std::move(sequence));
        }

        const std::uint32_t connects_end = reader.U32();
        const std::int32_t connect_count = Count(reader, kMaxEntries, "Mesh.Connects");
        SkipBytesForCount(reader, connect_count, 8u, "Mesh.Connects");
        ValidateLazyEnd(reader, connects_end, "Mesh.Connects");

        SkipPrimitiveBounds(reader, version);

        const std::uint32_t links_end = reader.U32();
        const std::int32_t link_count = Count(reader, kMaxEntries, "Mesh.VertLinks");
        SkipBytesForCount(reader, link_count, 4u, "Mesh.VertLinks");
        ValidateLazyEnd(reader, links_end, "Mesh.VertLinks");

        const std::int32_t texture_count = Count(reader, kMaxMaterials, "Mesh.Textures");
        for (std::int32_t index = 0; index < texture_count; ++index) {
            reader.CompactIndex();
        }
        const std::int32_t box_count = Count(reader, kMaxEntries, "Mesh.BoundingBoxes");
        SkipBytesForCount(reader, box_count, 25u, "Mesh.BoundingBoxes");
        const std::int32_t sphere_count = Count(reader, kMaxEntries, "Mesh.BoundingSpheres");
        SkipBytesForCount(reader, sphere_count, 16u, "Mesh.BoundingSpheres");

        result.frame_vertices = reader.I32();
        result.animation_frames = reader.I32();
        reader.U32();
        reader.U32();
        ReadVec3(reader);
        ReadVec3(reader);
        reader.I32();
        reader.I32();
        reader.I32();
        reader.U32();
        reader.U32();
        if (version == 65) {
            reader.F32();
        } else if (version >= 66) {
            const std::int32_t lod_count = Count(reader, kMaxMaterials, "Mesh.TextureLOD");
            SkipBytesForCount(reader, lod_count, 4u, "Mesh.TextureLOD");
        }

        const std::int32_t collapse_points = Count(reader, kMaxEntries, "LodMesh.CollapsePointThus");
        SkipBytesForCount(reader, collapse_points, 2u, "LodMesh.CollapsePointThus");
        const std::int32_t face_levels = Count(reader, kMaxTriangles, "LodMesh.FaceLevel");
        SkipBytesForCount(reader, face_levels, 2u, "LodMesh.FaceLevel");
        const std::int32_t face_count = Count(reader, kMaxTriangles, "LodMesh.Faces");
        SkipBytesForCount(reader, face_count, 8u, "LodMesh.Faces");
        const std::int32_t collapse_wedges = Count(reader, kMaxEntries, "LodMesh.CollapseWedgeThus");
        SkipBytesForCount(reader, collapse_wedges, 2u, "LodMesh.CollapseWedgeThus");
        const std::int32_t wedge_count = Count(reader, kMaxEntries, "LodMesh.Wedges");
        SkipBytesForCount(reader, wedge_count, 4u, "LodMesh.Wedges");
        const std::int32_t material_count = Count(reader, kMaxMaterials, "LodMesh.Materials");
        SkipBytesForCount(reader, material_count, 8u, "LodMesh.Materials");
        const std::int32_t special_faces = Count(reader, kMaxTriangles, "LodMesh.SpecialFaces");
        SkipBytesForCount(reader, special_faces, 8u, "LodMesh.SpecialFaces");
        reader.U32();
        reader.U32();
        reader.F32();
        reader.F32();
        reader.F32();
        reader.U32();
        reader.F32();
        reader.F32();
        const std::int32_t remap_count = Count(reader, kMaxEntries, "LodMesh.ReMapAnimVerts");
        SkipBytesForCount(reader, remap_count, 2u, "LodMesh.ReMapAnimVerts");
        reader.U32();

        const std::int32_t ext_wedge_count = Count(reader, kMaxEntries, "SkeletalMesh.ExtWedges");
        SkipBytesForCount(reader, ext_wedge_count, 12u, "SkeletalMesh.ExtWedges");

        const std::int32_t point_count = Count(reader, kMaxVertices, "SkeletalMesh.Points");
        result.reference_points.reserve(static_cast<std::size_t>(point_count));
        for (std::int32_t index = 0; index < point_count; ++index) {
            result.reference_points.push_back(ReadVec3(reader));
        }
        result.reference_point_count = result.reference_points.size();

        const std::int32_t bone_count = Count(reader, kMaxMaterials, "SkeletalMesh.RefSkeleton");
        result.bones.reserve(static_cast<std::size_t>(bone_count));
        for (std::int32_t index = 0; index < bone_count; ++index) {
            SkeletalReferenceBone bone;
            bone.name = NameAt(package, reader.CompactIndex());
            bone.flags = reader.U32();
            for (float& component : bone.orientation) {
                component = reader.F32();
            }
            bone.position = ReadVec3(reader);
            bone.length = reader.F32();
            bone.size = ReadVec3(reader);
            bone.child_count = reader.U32();
            bone.parent_index = reader.I32();
            if (bone.parent_index < 0 || bone.parent_index == index) {
                ++result.root_like_bones;
            } else if (bone.parent_index >= bone_count) {
                ++result.invalid_parent_bones;
            }
            result.bones.push_back(std::move(bone));
        }

        const std::int32_t weight_index_count = Count(
            reader, kMaxEntries, "SkeletalMesh.BoneWeightIndices"
        );
        result.weight_indices.reserve(static_cast<std::size_t>(weight_index_count));
        for (std::int32_t index = 0; index < weight_index_count; ++index) {
            SkeletalWeightIndexRecord record;
            record.first = reader.U32();
            record.second = reader.U32();
            result.weight_index_first_max = std::max(result.weight_index_first_max, record.first);
            result.weight_index_second_max = std::max(result.weight_index_second_max, record.second);
            result.weight_indices.push_back(record);
        }

        const std::int32_t weight_count = Count(reader, kMaxEntries, "SkeletalMesh.BoneWeights");
        result.weight_words.reserve(static_cast<std::size_t>(weight_count));
        for (std::int32_t index = 0; index < weight_count; ++index) {
            SkeletalWeightWord word;
            word.raw = reader.U32();
            std::memcpy(&word.as_float, &word.raw, sizeof(word.as_float));
            word.finite = std::isfinite(word.as_float);
            if (word.finite) {
                ++result.finite_weight_words;
                if (word.as_float >= 0.0f && word.as_float <= 1.0f) {
                    ++result.unit_interval_weight_words;
                }
            } else {
                ++result.nonfinite_weight_words;
            }
            result.weight_words.push_back(word);
        }

        const std::int32_t local_point_count = Count(reader, kMaxEntries, "SkeletalMesh.LocalPoints");
        result.local_points.reserve(static_cast<std::size_t>(local_point_count));
        for (std::int32_t index = 0; index < local_point_count; ++index) {
            result.local_points.push_back(ReadVec3(reader));
        }
        reader.U32();
        result.animation_reference = reader.CompactIndex();
        result.animation_object_name = ObjectNameAt(package, result.animation_reference);
        reader.U32();
        reader.Skip(48u, "SkeletalMesh.WeaponCoords");

        result.remaining_bytes = reader.Remaining();
        result.valid = !result.reference_points.empty() && !result.bones.empty();
        if (!result.valid) {
            result.error = "skeletal stream contains no reference points or bones";
        }
    } catch (const std::exception& exception) {
        result.error = exception.what();
        result.valid = false;
    }
    return result;
}

SkeletalMeshSkinningData LoadNamedSkeletalMeshSkinning(
    const std::filesystem::path& game_root,
    const std::string& package_name,
    const std::string& object_name
) {
    SkeletalMeshSkinningData result;
    const std::filesystem::path package_path = FindPackage(game_root, package_name);
    if (package_path.empty()) {
        result.error = "requested skeletal mesh package was not found";
        return result;
    }
    const PackageIndex package = LoadPackageIndex(package_path);
    if (!package.valid) {
        result.error = package.error.empty() ? "requested skeletal package is invalid" : package.error;
        return result;
    }
    const std::size_t export_index = FindSkeletalExport(package, object_name);
    if (export_index == std::numeric_limits<std::size_t>::max()) {
        result.error = "requested SkeletalMesh export was not found";
        return result;
    }
    return LoadSkeletalMeshSkinningExport(package, export_index);
}

}  // namespace hp2
