#include "hp2/ue_actor.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace hp2 {
namespace {

constexpr std::uint32_t kObjectHasStack = 0x02000000u;
constexpr std::uint32_t kObjectNative = 0x04000000u;
constexpr std::int32_t kMaxActorCount = 500'000;
constexpr std::size_t kMaxPropertyCount = 65'536;
constexpr std::size_t kMaxPropertyBytes = 64u * 1024u * 1024u;

std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

class PayloadReader {
public:
    explicit PayloadReader(
        const std::vector<std::uint8_t>& bytes,
        std::size_t position = 0
    ) : bytes_(bytes), position_(position) {
        if (position_ > bytes_.size()) {
            throw std::runtime_error("property offset is outside the object payload");
        }
    }

    std::size_t Tell() const { return position_; }
    std::size_t Remaining() const { return bytes_.size() - position_; }

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
            throw std::runtime_error("object property contains a non-finite float");
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
    std::size_t position_ = 0;
};

std::vector<std::uint8_t> ReadExportPayload(
    const PackageIndex& package,
    const ExportEntry& entry
) {
    std::ifstream input(package.summary.path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open object package payload");
    }
    input.seekg(entry.serial_offset, std::ios::beg);
    if (!input) {
        throw std::runtime_error("cannot seek to object payload");
    }
    std::vector<std::uint8_t> payload(static_cast<std::size_t>(entry.serial_size));
    input.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    if (input.gcount() != static_cast<std::streamsize>(payload.size())) {
        throw std::runtime_error("short read while loading object payload");
    }
    return payload;
}

std::int32_t ReadArrayIndex(PayloadReader& reader) {
    const std::uint8_t first = reader.U8();
    if ((first & 0xc0u) == 0xc0u) {
        const std::uint8_t second = reader.U8();
        const std::uint8_t third = reader.U8();
        const std::uint8_t fourth = reader.U8();
        return static_cast<std::int32_t>(
            (static_cast<std::uint32_t>(first & 0x3fu) << 24u)
            | (static_cast<std::uint32_t>(second) << 16u)
            | (static_cast<std::uint32_t>(third) << 8u)
            | static_cast<std::uint32_t>(fourth)
        );
    }
    if ((first & 0x80u) != 0u) {
        return static_cast<std::int32_t>(
            (static_cast<std::uint16_t>(first & 0x7fu) << 8u) | reader.U8()
        );
    }
    return first;
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

std::string NameAt(const PackageIndex& package, std::int32_t index, const char* description) {
    if (index < 0 || static_cast<std::size_t>(index) >= package.names.size()) {
        throw std::runtime_error(std::string(description) + " is outside the package name table");
    }
    return package.names[static_cast<std::size_t>(index)].value;
}

ObjectProperties ReadTaggedProperties(
    PayloadReader& reader,
    const PackageIndex& package
) {
    ObjectProperties result;
    for (std::size_t property_index = 0; property_index < kMaxPropertyCount; ++property_index) {
        const std::string property_name = NameAt(
            package, reader.CompactIndex(), "property name index"
        );
        if (Lowercase(property_name) == "none") {
            result.native_data_offset = reader.Tell();
            result.valid = true;
            return result;
        }

        SerializedProperty property;
        property.name = property_name;
        const std::uint8_t info = reader.U8();
        property.type = info & 0x0fu;
        if (property.type == 0u) {
            throw std::runtime_error("invalid tagged property type");
        }
        if (property.type == 10u) {
            property.struct_name = NameAt(
                package, reader.CompactIndex(), "property struct name index"
            );
        }
        const std::size_t serialized_size = TaggedPropertySize(reader, info);
        if (serialized_size > kMaxPropertyBytes) {
            throw std::runtime_error("tagged property payload exceeds the safety limit");
        }
        if (property.type == 3u) {
            property.bool_value = (info & 0x80u) != 0u;
        } else {
            if ((info & 0x80u) != 0u) {
                property.array_index = ReadArrayIndex(reader);
            }
            property.bytes = reader.Bytes(serialized_size, "tagged property payload");
        }
        result.properties.push_back(std::move(property));
    }
    throw std::runtime_error("tagged property list has no None terminator");
}

ObjectProperties ReadProperties(
    PayloadReader& reader,
    const PackageIndex& package,
    const ExportEntry& entry
) {
    ObjectProperties result;
    if ((entry.object_flags & kObjectNative) != 0u) {
        result.error = "native object property streams are not supported";
        return result;
    }
    if ((entry.object_flags & kObjectHasStack) != 0u) {
        const std::int32_t node = reader.CompactIndex();
        reader.CompactIndex();
        reader.U64();
        reader.U32();
        if (node != 0) {
            reader.CompactIndex();
        }
        result.has_state_frame = true;
    }
    ObjectProperties properties = ReadTaggedProperties(reader, package);
    properties.has_state_frame = result.has_state_frame;
    return properties;
}

bool IsClassDefaultProperty(const std::string& name) {
    static const std::set<std::string> kNames = {
        "ambientglow", "bhidden", "collisionheight", "collisionradius",
        "drawscale", "drawscale3d", "drawtype", "fatness", "mesh",
        "multiskins", "prepivot", "skin", "staticmesh", "style", "texture"
    };
    return kNames.find(Lowercase(name)) != kNames.end();
}

std::size_t ClassDefaultScore(const ObjectProperties& properties) {
    std::size_t interesting = 0;
    std::size_t strongly_typed = 0;
    for (const SerializedProperty& property : properties.properties) {
        if (!IsClassDefaultProperty(property.name)) {
            continue;
        }
        ++interesting;
        const std::string name = Lowercase(property.name);
        if ((name == "mesh" || name == "staticmesh" || name == "skin")
            && property.type == 5u) {
            ++strongly_typed;
        } else if (name == "drawscale" && property.type == 4u
                   && property.bytes.size() == 4u) {
            ++strongly_typed;
        } else if ((name == "drawscale3d" || name == "prepivot")
                   && property.type == 10u && property.bytes.size() == 12u) {
            ++strongly_typed;
        } else if (name == "bhidden" && property.type == 3u) {
            ++strongly_typed;
        }
    }
    if (interesting == 0 || strongly_typed == 0) {
        return 0;
    }
    return strongly_typed * 1'000'000u + interesting * 10'000u
        + properties.properties.size();
}

const SerializedProperty* FindProperty(
    const ObjectProperties& object,
    const char* name
) {
    const std::string target = Lowercase(name);
    for (const auto& property : object.properties) {
        if (Lowercase(property.name) == target) {
            return &property;
        }
    }
    return nullptr;
}

bool DecodeVec3(const SerializedProperty* property, Vec3& value) {
    if (property == nullptr || property->bytes.size() != 12u) {
        return false;
    }
    PayloadReader reader(property->bytes);
    value = {reader.F32(), reader.F32(), reader.F32()};
    return true;
}

bool DecodeRotator(const SerializedProperty* property, Rotator& value) {
    if (property == nullptr || property->bytes.size() != 12u) {
        return false;
    }
    PayloadReader reader(property->bytes);
    value = {reader.I32(), reader.I32(), reader.I32()};
    return true;
}

bool DecodeFloat(const SerializedProperty* property, float& value) {
    if (property == nullptr || property->bytes.size() != 4u) {
        return false;
    }
    PayloadReader reader(property->bytes);
    value = reader.F32();
    return true;
}

std::int32_t DecodeObjectReference(const SerializedProperty* property) {
    if (property == nullptr || property->bytes.empty()) {
        return 0;
    }
    try {
        PayloadReader reader(property->bytes);
        const std::int32_t value = reader.CompactIndex();
        return reader.Remaining() == 0 ? value : 0;
    } catch (const std::exception&) {
        return 0;
    }
}

std::size_t FindLevelExport(const PackageIndex& package) {
    for (std::size_t index = 0; index < package.exports.size(); ++index) {
        if (Lowercase(package.exports[index].class_name) == "level") {
            return index;
        }
    }
    return std::numeric_limits<std::size_t>::max();
}

}  // namespace

ObjectProperties LoadObjectProperties(
    const PackageIndex& package,
    std::size_t export_index
) {
    ObjectProperties result;
    if (!package.valid) {
        result.error = package.error.empty() ? "package index is invalid" : package.error;
        return result;
    }
    if (package.summary.file_version < 64 || package.summary.file_version >= 100) {
        result.error = "object property reader currently supports UE1 versions 64-99";
        return result;
    }
    if (export_index >= package.exports.size()) {
        result.error = "object export index is outside the export table";
        return result;
    }
    const ExportEntry& entry = package.exports[export_index];
    if (entry.serial_size <= 0) {
        result.error = "object export has no serialized payload";
        return result;
    }
    try {
        const std::vector<std::uint8_t> payload = ReadExportPayload(package, entry);
        PayloadReader reader(payload);
        return ReadProperties(reader, package, entry);
    } catch (const std::exception& exception) {
        result.error = exception.what();
        return result;
    }
}

ObjectProperties ScanClassDefaultProperties(
    const PackageIndex& package,
    std::size_t export_index,
    std::size_t max_scan_bytes
) {
    ObjectProperties result;
    if (!package.valid) {
        result.error = package.error.empty() ? "package index is invalid" : package.error;
        return result;
    }
    if (package.summary.file_version < 64 || package.summary.file_version >= 100) {
        result.error = "class-default reader currently supports UE1 versions 64-99";
        return result;
    }
    if (export_index >= package.exports.size()) {
        result.error = "class export index is outside the export table";
        return result;
    }
    const ExportEntry& entry = package.exports[export_index];
    if (Lowercase(entry.class_name) != "class") {
        result.error = "selected export is not a UClass";
        return result;
    }
    if (entry.serial_size <= 0 || entry.serial_offset < 0 || max_scan_bytes == 0) {
        result.error = "class export has no bounded serialized tail";
        return result;
    }

    try {
        const std::size_t serial_size = static_cast<std::size_t>(entry.serial_size);
        const std::size_t scan_size = std::min(serial_size, max_scan_bytes);
        const std::int64_t tail_offset = static_cast<std::int64_t>(entry.serial_offset)
            + static_cast<std::int64_t>(serial_size - scan_size);
        if (tail_offset < 0) {
            throw std::runtime_error("class export tail offset is invalid");
        }
        std::ifstream input(package.summary.path, std::ios::binary);
        if (!input) {
            throw std::runtime_error("cannot open class package payload");
        }
        input.seekg(tail_offset, std::ios::beg);
        if (!input) {
            throw std::runtime_error("cannot seek to class package tail");
        }
        std::vector<std::uint8_t> tail(scan_size);
        input.read(reinterpret_cast<char*>(tail.data()), static_cast<std::streamsize>(tail.size()));
        if (input.gcount() != static_cast<std::streamsize>(tail.size())) {
            throw std::runtime_error("short read while loading class package tail");
        }

        std::size_t best_score = 0;
        std::size_t best_start = tail.size();
        for (std::size_t candidate_start = 0; candidate_start < tail.size(); ++candidate_start) {
            try {
                PayloadReader name_reader(tail, candidate_start);
                const std::int32_t name_index = name_reader.CompactIndex();
                if (name_index < 0 || static_cast<std::size_t>(name_index) >= package.names.size()
                    || !IsClassDefaultProperty(
                        package.names[static_cast<std::size_t>(name_index)].value
                    )) {
                    continue;
                }
                PayloadReader reader(tail, candidate_start);
                ObjectProperties candidate = ReadTaggedProperties(reader, package);
                if (!candidate.valid || candidate.native_data_offset != tail.size()) {
                    continue;
                }
                const std::size_t score = ClassDefaultScore(candidate);
                if (score > best_score || (score == best_score && candidate_start < best_start)) {
                    best_score = score;
                    best_start = candidate_start;
                    result = std::move(candidate);
                }
            } catch (const std::exception&) {
                // Bytecode and native class metadata are expected before the default stream.
            }
        }
        if (best_score == 0) {
            result = {};
            result.error = "no bounded class-default property tail was found";
        }
    } catch (const std::exception& exception) {
        result = {};
        result.error = exception.what();
    }
    return result;
}

const SerializedProperty* FindObjectProperty(
    const ObjectProperties& object,
    const char* name
) {
    return FindProperty(object, name);
}

bool DecodePropertyVec3(const SerializedProperty* property, Vec3& value) {
    return DecodeVec3(property, value);
}

bool DecodePropertyRotator(const SerializedProperty* property, Rotator& value) {
    return DecodeRotator(property, value);
}

bool DecodePropertyFloat(const SerializedProperty* property, float& value) {
    return DecodeFloat(property, value);
}

bool DecodePropertyBool(const SerializedProperty* property, bool& value) {
    if (property == nullptr || property->type != 3u) {
        return false;
    }
    value = property->bool_value;
    return true;
}

std::int32_t DecodePropertyObjectReference(const SerializedProperty* property) {
    return DecodeObjectReference(property);
}

LevelActorCensus LoadLevelActorCensus(const PackageIndex& package) {
    LevelActorCensus result;
    if (!package.valid) {
        result.error = package.error.empty() ? "package index is invalid" : package.error;
        return result;
    }
    if (package.summary.file_version < 64 || package.summary.file_version >= 100) {
        result.error = "level actor reader currently supports UE1 versions 64-99";
        return result;
    }
    const std::size_t level_index = FindLevelExport(package);
    if (level_index == std::numeric_limits<std::size_t>::max()) {
        result.error = "package contains no serialized Level export";
        return result;
    }
    result.level_export_index = level_index;
    result.level_object_name = package.exports[level_index].object_name;

    try {
        const ExportEntry& level_entry = package.exports[level_index];
        const std::vector<std::uint8_t> payload = ReadExportPayload(package, level_entry);
        PayloadReader reader(payload);
        const ObjectProperties level_properties = ReadProperties(reader, package, level_entry);
        if (!level_properties.valid) {
            throw std::runtime_error(level_properties.error);
        }
        const std::int32_t actor_count = reader.I32();
        const std::int32_t actor_capacity = reader.I32();
        if (actor_count < 0 || actor_count > kMaxActorCount
            || actor_capacity < actor_count || actor_capacity > kMaxActorCount) {
            throw std::runtime_error("Level actor array count or capacity is unreasonable");
        }
        result.actor_references.reserve(static_cast<std::size_t>(actor_count));
        for (std::int32_t index = 0; index < actor_count; ++index) {
            result.actor_references.push_back(reader.CompactIndex());
        }
        result.actor_reference_count = result.actor_references.size();

        std::map<std::string, std::size_t> class_counts;
        for (const std::int32_t reference : result.actor_references) {
            if (reference == 0) {
                continue;
            }
            ++result.non_null_actor_references;
            if (reference < 0) {
                ++result.actor_parse_failures;
                continue;
            }
            const std::int64_t export_offset = static_cast<std::int64_t>(reference) - 1;
            if (export_offset < 0
                || static_cast<std::size_t>(export_offset) >= package.exports.size()) {
                ++result.actor_parse_failures;
                continue;
            }

            ActorInstance actor;
            actor.object_reference = reference;
            actor.export_index = static_cast<std::size_t>(export_offset);
            const ExportEntry& entry = package.exports[actor.export_index];
            actor.class_reference = entry.class_index;
            actor.object_name = entry.object_name;
            actor.class_name = entry.class_name;
            ++class_counts[actor.class_name];

            const ObjectProperties properties = LoadObjectProperties(package, actor.export_index);
            actor.properties_valid = properties.valid;
            actor.property_count = properties.properties.size();
            if (!properties.valid) {
                actor.error = properties.error;
                ++result.actor_parse_failures;
                result.actors.push_back(std::move(actor));
                continue;
            }
            ++result.parsed_actor_count;
            actor.has_location = DecodeVec3(FindProperty(properties, "Location"), actor.location);
            actor.has_rotation = DecodeRotator(FindProperty(properties, "Rotation"), actor.rotation);
            actor.has_pre_pivot = DecodeVec3(FindProperty(properties, "PrePivot"), actor.pre_pivot);
            actor.has_draw_scale = DecodeFloat(FindProperty(properties, "DrawScale"), actor.draw_scale);
            actor.has_draw_scale_3d = DecodeVec3(
                FindProperty(properties, "DrawScale3D"), actor.draw_scale_3d
            );
            const SerializedProperty* hidden = FindProperty(properties, "bHidden");
            actor.has_hidden = hidden != nullptr && hidden->type == 3u;
            actor.hidden = actor.has_hidden && hidden->bool_value;
            actor.mesh_reference = DecodeObjectReference(FindProperty(properties, "Mesh"));
            actor.static_mesh_reference = DecodeObjectReference(
                FindProperty(properties, "StaticMesh")
            );

            result.actors_with_location += actor.has_location ? 1u : 0u;
            result.actors_with_rotation += actor.has_rotation ? 1u : 0u;
            result.direct_mesh_references += actor.mesh_reference != 0 ? 1u : 0u;
            result.direct_static_mesh_references += actor.static_mesh_reference != 0 ? 1u : 0u;
            result.actors.push_back(std::move(actor));
        }

        result.class_inventory.reserve(class_counts.size());
        for (const auto& item : class_counts) {
            result.class_inventory.push_back({item.first, item.second});
        }
        std::sort(
            result.class_inventory.begin(), result.class_inventory.end(),
            [](const auto& left, const auto& right) {
                return left.count != right.count
                    ? left.count > right.count
                    : left.class_name < right.class_name;
            }
        );
        result.valid = true;
    } catch (const std::exception& exception) {
        result.error = exception.what();
        result.actor_references.clear();
        result.actors.clear();
        result.class_inventory.clear();
    }
    return result;
}

}  // namespace hp2
