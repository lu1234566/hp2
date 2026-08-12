#include "hp2/ue_skeletal.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

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

std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

struct LayoutMetrics {
    std::size_t interval_union = 0;
    std::size_t interval_overlap = 0;
    std::size_t interval_gaps = 0;
    std::uint64_t interval_count_sum = 0;
    std::size_t points_with_weights = 0;
    std::size_t max_influences_per_point = 0;
    std::uint64_t point_weight_sum_min = 0;
    std::uint64_t point_weight_sum_max = 0;
    bool confirmed = false;
};

LayoutMetrics MeasureLayout(const hp2::SkeletalMeshSkinningData& data) {
    LayoutMetrics metrics;
    const std::size_t influence_count = data.weight_words.size();
    std::vector<std::uint16_t> interval_coverage(influence_count, 0u);
    for (const auto& record : data.weight_indices) {
        const std::size_t first = record.first & 0xffffu;
        const std::size_t count = (record.first >> 16u) & 0xffffu;
        metrics.interval_count_sum += count;
        if (first + count > influence_count) {
            continue;
        }
        for (std::size_t slot = first; slot < first + count; ++slot) {
            ++interval_coverage[slot];
        }
    }
    for (const auto count : interval_coverage) {
        metrics.interval_union += count > 0u ? 1u : 0u;
        metrics.interval_overlap += count > 1u ? 1u : 0u;
        metrics.interval_gaps += count == 0u ? 1u : 0u;
    }

    std::vector<std::uint64_t> point_weight_sums(data.reference_point_count, 0u);
    std::vector<std::size_t> point_influence_counts(data.reference_point_count, 0u);
    bool all_point_indices_valid = true;
    for (const auto& word : data.weight_words) {
        const std::size_t point_index = word.raw & 0xffffu;
        const std::uint32_t weight = (word.raw >> 16u) & 0xffffu;
        if (point_index >= data.reference_point_count) {
            all_point_indices_valid = false;
            continue;
        }
        point_weight_sums[point_index] += weight;
        ++point_influence_counts[point_index];
    }

    bool have_weighted_point = false;
    for (std::size_t point_index = 0; point_index < point_weight_sums.size(); ++point_index) {
        if (point_influence_counts[point_index] == 0u) {
            continue;
        }
        ++metrics.points_with_weights;
        metrics.max_influences_per_point = std::max(
            metrics.max_influences_per_point, point_influence_counts[point_index]
        );
        const std::uint64_t sum = point_weight_sums[point_index];
        if (!have_weighted_point) {
            metrics.point_weight_sum_min = sum;
            metrics.point_weight_sum_max = sum;
            have_weighted_point = true;
        } else {
            metrics.point_weight_sum_min = std::min(metrics.point_weight_sum_min, sum);
            metrics.point_weight_sum_max = std::max(metrics.point_weight_sum_max, sum);
        }
    }

    metrics.confirmed = data.weight_indices.size() == data.bones.size()
        && data.weight_words.size() == data.local_points.size()
        && data.weight_index_second_max == 0u
        && metrics.interval_count_sum == influence_count
        && metrics.interval_union == influence_count
        && metrics.interval_overlap == 0u
        && metrics.interval_gaps == 0u
        && all_point_indices_valid
        && metrics.points_with_weights == data.reference_point_count
        && metrics.point_weight_sum_min >= 65530u
        && metrics.point_weight_sum_max <= 65535u;
    return metrics;
}

struct DVec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Quat {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 1.0;
};

struct Transform {
    Quat rotation;
    DVec3 position;
};

DVec3 Add(const DVec3& a, const DVec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

DVec3 Scale(const DVec3& value, double scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}

DVec3 ToDVec3(const hp2::Vec3& value) {
    return {value.x, value.y, value.z};
}

Quat Normalize(Quat value) {
    const double length = std::sqrt(
        value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w
    );
    if (length <= 1.0e-12) {
        return {};
    }
    value.x /= length;
    value.y /= length;
    value.z /= length;
    value.w /= length;
    return value;
}

Quat Conjugate(Quat value) {
    value.x = -value.x;
    value.y = -value.y;
    value.z = -value.z;
    return value;
}

Quat Multiply(const Quat& a, const Quat& b) {
    return Normalize({
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    });
}

DVec3 Rotate(const Quat& q_in, const DVec3& value) {
    const Quat q = Normalize(q_in);
    const DVec3 u{q.x, q.y, q.z};
    const double dot_uv = u.x * value.x + u.y * value.y + u.z * value.z;
    const double dot_uu = u.x * u.x + u.y * u.y + u.z * u.z;
    const DVec3 cross{
        u.y * value.z - u.z * value.y,
        u.z * value.x - u.x * value.z,
        u.x * value.y - u.y * value.x,
    };
    return Add(Add(Scale(u, 2.0 * dot_uv), Scale(value, q.w * q.w - dot_uu)),
               Scale(cross, 2.0 * q.w));
}

Quat ReferenceBoneRotation(const hp2::SkeletalReferenceBone& bone) {
    return Conjugate(Normalize({
        bone.orientation[0], bone.orientation[1], bone.orientation[2], bone.orientation[3]
    }));
}

bool ResolveReferenceTransform(
    const hp2::SkeletalMeshSkinningData& data,
    std::size_t bone_index,
    std::vector<Transform>& transforms,
    std::vector<std::uint8_t>& states
) {
    if (bone_index >= data.bones.size()) {
        return false;
    }
    if (states[bone_index] == 2u) {
        return true;
    }
    if (states[bone_index] == 1u) {
        return false;
    }
    states[bone_index] = 1u;
    const auto& bone = data.bones[bone_index];
    const Transform local{ReferenceBoneRotation(bone), ToDVec3(bone.position)};
    const std::int32_t parent = bone.parent_index;
    if (parent < 0 || parent == static_cast<std::int32_t>(bone_index)) {
        transforms[bone_index] = local;
    } else {
        if (parent >= static_cast<std::int32_t>(data.bones.size())
            || !ResolveReferenceTransform(
                data, static_cast<std::size_t>(parent), transforms, states)) {
            return false;
        }
        const Transform& parent_transform = transforms[static_cast<std::size_t>(parent)];
        transforms[bone_index].rotation = Multiply(parent_transform.rotation, local.rotation);
        transforms[bone_index].position = Add(
            parent_transform.position,
            Rotate(parent_transform.rotation, local.position)
        );
    }
    states[bone_index] = 2u;
    return true;
}

struct BindValidation {
    bool valid = false;
    std::size_t points = 0;
    double rms_error = 0.0;
    double max_error = 0.0;
};

BindValidation ValidateReferenceBind(const hp2::SkeletalMeshSkinningData& data) {
    BindValidation result;
    if (data.bones.empty() || data.reference_points.empty()
        || data.weight_indices.size() != data.bones.size()
        || data.weight_words.size() != data.local_points.size()) {
        return result;
    }

    std::vector<Transform> transforms(data.bones.size());
    std::vector<std::uint8_t> states(data.bones.size(), 0u);
    for (std::size_t index = 0; index < data.bones.size(); ++index) {
        if (!ResolveReferenceTransform(data, index, transforms, states)) {
            return result;
        }
    }

    std::vector<std::int32_t> influence_bone(data.weight_words.size(), -1);
    for (std::size_t bone_index = 0; bone_index < data.weight_indices.size(); ++bone_index) {
        const std::uint32_t packed = data.weight_indices[bone_index].first;
        const std::size_t first = packed & 0xffffu;
        const std::size_t count = (packed >> 16u) & 0xffffu;
        if (first + count > influence_bone.size()) {
            return result;
        }
        for (std::size_t slot = first; slot < first + count; ++slot) {
            if (influence_bone[slot] != -1) {
                return result;
            }
            influence_bone[slot] = static_cast<std::int32_t>(bone_index);
        }
    }
    if (std::find(influence_bone.begin(), influence_bone.end(), -1) != influence_bone.end()) {
        return result;
    }

    std::vector<DVec3> accum(data.reference_points.size());
    std::vector<std::uint64_t> sums(data.reference_points.size(), 0u);
    for (std::size_t slot = 0; slot < data.weight_words.size(); ++slot) {
        const std::uint32_t packed = data.weight_words[slot].raw;
        const std::size_t point_index = packed & 0xffffu;
        const std::uint32_t weight = (packed >> 16u) & 0xffffu;
        const std::int32_t bone_index = influence_bone[slot];
        if (point_index >= accum.size() || bone_index < 0) {
            return result;
        }
        const Transform& transform = transforms[static_cast<std::size_t>(bone_index)];
        const DVec3 placed = Add(
            transform.position,
            Rotate(transform.rotation, ToDVec3(data.local_points[slot]))
        );
        accum[point_index] = Add(accum[point_index], Scale(placed, weight));
        sums[point_index] += weight;
    }

    double squared_sum = 0.0;
    double max_squared = 0.0;
    for (std::size_t point_index = 0; point_index < data.reference_points.size(); ++point_index) {
        if (sums[point_index] == 0u) {
            continue;
        }
        const DVec3 predicted = Scale(accum[point_index], 1.0 / static_cast<double>(sums[point_index]));
        const DVec3 reference = ToDVec3(data.reference_points[point_index]);
        const double dx = predicted.x - reference.x;
        const double dy = predicted.y - reference.y;
        const double dz = predicted.z - reference.z;
        const double squared = dx * dx + dy * dy + dz * dz;
        squared_sum += squared;
        max_squared = std::max(max_squared, squared);
        ++result.points;
    }
    if (result.points == 0u) {
        return result;
    }
    result.rms_error = std::sqrt(squared_sum / static_cast<double>(result.points));
    result.max_error = std::sqrt(max_squared);
    result.valid = std::isfinite(result.rms_error) && std::isfinite(result.max_error);
    return result;
}

struct ObjectCandidate {
    std::string kind;
    std::string class_name;
    std::string object_name;
};

std::vector<ObjectCandidate> FindAnimationCandidates(const hp2::SkeletalMeshSkinningData& data) {
    std::vector<ObjectCandidate> result;
    const hp2::PackageIndex package = hp2::LoadPackageIndex(data.package_path);
    if (!package.valid) {
        return result;
    }
    auto is_candidate = [](const std::string& class_name, const std::string& object_name) {
        const std::string class_lower = Lowercase(class_name);
        const std::string object_lower = Lowercase(object_name);
        return class_lower.find("anim") != std::string::npos
            || object_lower.find("anim") != std::string::npos;
    };
    for (const auto& entry : package.exports) {
        if (is_candidate(entry.class_name, entry.object_name)) {
            result.push_back({"export", entry.class_name, entry.object_name});
        }
    }
    for (const auto& entry : package.imports) {
        if (is_candidate(entry.class_name, entry.object_name)) {
            result.push_back({"import", entry.class_name, entry.object_name});
        }
    }
    if (result.size() > 96u) {
        result.resize(96u);
    }
    return result;
}

std::uint32_t ReadU32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset + 4u > bytes.size()) {
        return 0u;
    }
    return static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1u]) << 8u)
        | (static_cast<std::uint32_t>(bytes[offset + 2u]) << 16u)
        | (static_cast<std::uint32_t>(bytes[offset + 3u]) << 24u);
}

bool DecodeCompactExact(
    const std::vector<std::uint8_t>& bytes,
    std::size_t start,
    std::size_t length,
    std::int32_t& value_out
) {
    if (length == 0u || length > 5u || start + length > bytes.size()) {
        return false;
    }
    const std::uint8_t first = bytes[start];
    const bool negative = (first & 0x80u) != 0u;
    std::uint32_t value = first & 0x3fu;
    bool more = (first & 0x40u) != 0u;
    std::uint32_t shift = 6u;
    std::size_t used = 1u;
    while (more) {
        if (used >= length || used >= 5u || shift >= 32u) {
            return false;
        }
        const std::uint8_t byte = bytes[start + used];
        const std::uint32_t payload = byte & 0x7fu;
        if (shift == 27u && payload > 0x0fu) {
            return false;
        }
        value |= payload << shift;
        shift += 7u;
        more = (byte & 0x80u) != 0u;
        ++used;
    }
    if (used != length || value > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
        return false;
    }
    const auto signed_value = static_cast<std::int32_t>(value);
    value_out = negative ? -signed_value : signed_value;
    return true;
}

struct TailCandidate {
    std::size_t compact_bytes = 0;
    std::int32_t compact_value = 0;
    std::uint32_t before_u32 = 0;
    std::uint32_t after_u32 = 0;
    std::string reference_kind;
    std::string reference_class;
    std::string reference_object;
    std::string name_value;
};

void ResolveReference(
    const hp2::PackageIndex& package,
    std::int32_t reference,
    TailCandidate& candidate
) {
    if (reference == 0) {
        candidate.reference_kind = "none";
    } else if (reference > 0) {
        const std::size_t index = static_cast<std::size_t>(reference - 1);
        if (index < package.exports.size()) {
            candidate.reference_kind = "export";
            candidate.reference_class = package.exports[index].class_name;
            candidate.reference_object = package.exports[index].object_name;
        }
    } else {
        const std::size_t index = static_cast<std::size_t>(-static_cast<std::int64_t>(reference) - 1);
        if (index < package.imports.size()) {
            candidate.reference_kind = "import";
            candidate.reference_class = package.imports[index].class_name;
            candidate.reference_object = package.imports[index].object_name;
        }
    }
    if (reference >= 0 && static_cast<std::size_t>(reference) < package.names.size()) {
        candidate.name_value = package.names[static_cast<std::size_t>(reference)].value;
    }
}

std::vector<TailCandidate> InspectPostLocalTail(const hp2::SkeletalMeshSkinningData& data) {
    std::vector<TailCandidate> result;
    const hp2::PackageIndex package = hp2::LoadPackageIndex(data.package_path);
    if (!package.valid) {
        return result;
    }
    const hp2::ExportEntry* mesh_export = nullptr;
    for (const auto& entry : package.exports) {
        if (Lowercase(entry.class_name) == "skeletalmesh"
            && Lowercase(entry.object_name) == Lowercase(data.object_name)) {
            mesh_export = &entry;
            break;
        }
    }
    if (mesh_export == nullptr || mesh_export->serial_size <= 0 || mesh_export->serial_offset < 0) {
        return result;
    }
    std::ifstream input(data.package_path, std::ios::binary);
    if (!input) {
        return result;
    }
    input.seekg(mesh_export->serial_offset, std::ios::beg);
    std::vector<std::uint8_t> payload(static_cast<std::size_t>(mesh_export->serial_size));
    input.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    if (input.gcount() != static_cast<std::streamsize>(payload.size()) || payload.size() < 57u) {
        return result;
    }

    const std::size_t compact_end = payload.size() - 48u - 4u;
    for (std::size_t length = 1u; length <= 5u; ++length) {
        if (compact_end < length + 4u) {
            continue;
        }
        const std::size_t compact_start = compact_end - length;
        std::int32_t value = 0;
        if (!DecodeCompactExact(payload, compact_start, length, value)) {
            continue;
        }
        TailCandidate candidate;
        candidate.compact_bytes = length;
        candidate.compact_value = value;
        candidate.before_u32 = ReadU32(payload, compact_start - 4u);
        candidate.after_u32 = ReadU32(payload, compact_end);
        ResolveReference(package, value, candidate);
        result.push_back(std::move(candidate));
    }
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: hp2_skeletal_probe <game-root> <package-name> <object-name>\n";
        return 64;
    }

    const hp2::SkeletalMeshSkinningData data = hp2::LoadNamedSkeletalMeshSkinning(
        std::filesystem::path(argv[1]), argv[2], argv[3]
    );
    const LayoutMetrics layout = MeasureLayout(data);
    const BindValidation bind = ValidateReferenceBind(data);
    const auto animation_candidates = FindAnimationCandidates(data);
    const auto tail_candidates = InspectPostLocalTail(data);

    float min_float = std::numeric_limits<float>::infinity();
    float max_float = -std::numeric_limits<float>::infinity();
    for (const auto& word : data.weight_words) {
        if (word.finite) {
            min_float = std::min(min_float, word.as_float);
            max_float = std::max(max_float, word.as_float);
        }
    }
    const bool have_finite_float = data.finite_weight_words > 0u;

    std::cout << "{\n"
              << "  \"schema\": \"hp2-skeletal-probe-v5\",\n"
              << "  \"valid\": " << (data.valid ? "true" : "false") << ",\n"
              << "  \"package_name\": \"" << JsonEscape(data.package_name) << "\",\n"
              << "  \"object_name\": \"" << JsonEscape(data.object_name) << "\",\n"
              << "  \"version\": " << data.file_version << ",\n"
              << "  \"frame_vertices\": " << data.frame_vertices << ",\n"
              << "  \"animation_frames\": " << data.animation_frames << ",\n"
              << "  \"sequence_count\": " << data.sequences.size() << ",\n"
              << "  \"reference_points\": " << data.reference_point_count << ",\n"
              << "  \"bones\": " << data.bones.size() << ",\n"
              << "  \"root_like_bones\": " << data.root_like_bones << ",\n"
              << "  \"invalid_parent_bones\": " << data.invalid_parent_bones << ",\n"
              << "  \"weight_index_records\": " << data.weight_indices.size() << ",\n"
              << "  \"weight_index_first_max\": " << data.weight_index_first_max << ",\n"
              << "  \"weight_index_second_max\": " << data.weight_index_second_max << ",\n"
              << "  \"weight_words\": " << data.weight_words.size() << ",\n"
              << "  \"local_points\": " << data.local_points.size() << ",\n"
              << "  \"layout_confirmed\": " << (layout.confirmed ? "true" : "false") << ",\n"
              << "  \"influence_count_sum\": " << layout.interval_count_sum << ",\n"
              << "  \"influence_interval_union\": " << layout.interval_union << ",\n"
              << "  \"influence_interval_overlap\": " << layout.interval_overlap << ",\n"
              << "  \"influence_interval_gaps\": " << layout.interval_gaps << ",\n"
              << "  \"points_with_weights\": " << layout.points_with_weights << ",\n"
              << "  \"max_influences_per_point\": " << layout.max_influences_per_point << ",\n"
              << "  \"point_weight_sum_min\": " << layout.point_weight_sum_min << ",\n"
              << "  \"point_weight_sum_max\": " << layout.point_weight_sum_max << ",\n"
              << "  \"bind_valid\": " << (bind.valid ? "true" : "false") << ",\n"
              << "  \"bind_rule\": \"hierarchy_parent_times_local_xyzw_conjugate\",\n"
              << "  \"bind_points\": " << bind.points << ",\n"
              << "  \"bind_rms_error\": " << bind.rms_error << ",\n"
              << "  \"bind_max_error\": " << bind.max_error << ",\n"
              << "  \"finite_weight_words\": " << data.finite_weight_words << ",\n"
              << "  \"unit_interval_weight_words\": " << data.unit_interval_weight_words << ",\n"
              << "  \"nonfinite_weight_words\": " << data.nonfinite_weight_words << ",\n"
              << "  \"finite_weight_min\": ";
    if (have_finite_float) {
        std::cout << min_float;
    } else {
        std::cout << "null";
    }
    std::cout << ",\n  \"finite_weight_max\": ";
    if (have_finite_float) {
        std::cout << max_float;
    } else {
        std::cout << "null";
    }
    std::cout << ",\n"
              << "  \"remaining_bytes\": " << data.remaining_bytes << ",\n"
              << "  \"animation_object_candidates\": [\n";
    for (std::size_t index = 0; index < animation_candidates.size(); ++index) {
        const auto& candidate = animation_candidates[index];
        std::cout << "    {\"kind\": \"" << JsonEscape(candidate.kind)
                  << "\", \"class\": \"" << JsonEscape(candidate.class_name)
                  << "\", \"object\": \"" << JsonEscape(candidate.object_name) << "\"}"
                  << (index + 1u == animation_candidates.size() ? "" : ",") << '\n';
    }
    std::cout << "  ],\n"
              << "  \"post_local_tail_candidates\": [\n";
    for (std::size_t index = 0; index < tail_candidates.size(); ++index) {
        const auto& candidate = tail_candidates[index];
        std::cout << "    {\"compact_bytes\": " << candidate.compact_bytes
                  << ", \"value\": " << candidate.compact_value
                  << ", \"before_u32\": " << candidate.before_u32
                  << ", \"after_u32\": " << candidate.after_u32
                  << ", \"reference_kind\": \"" << JsonEscape(candidate.reference_kind)
                  << "\", \"reference_class\": \"" << JsonEscape(candidate.reference_class)
                  << "\", \"reference_object\": \"" << JsonEscape(candidate.reference_object)
                  << "\", \"name_value\": \"" << JsonEscape(candidate.name_value) << "\"}"
                  << (index + 1u == tail_candidates.size() ? "" : ",") << '\n';
    }
    std::cout << "  ],\n"
              << "  \"sequences\": [\n";
    for (std::size_t index = 0; index < data.sequences.size(); ++index) {
        const auto& sequence = data.sequences[index];
        std::cout << "    {\"name\": \"" << JsonEscape(sequence.name)
                  << "\", \"group\": \"" << JsonEscape(sequence.group)
                  << "\", \"start_frame\": " << sequence.start_frame
                  << ", \"frame_count\": " << sequence.frame_count
                  << ", \"notify_count\": " << sequence.notify_count
                  << ", \"rate\": " << sequence.rate << "}"
                  << (index + 1u == data.sequences.size() ? "" : ",") << '\n';
    }
    std::cout << "  ]";
    if (!data.error.empty()) {
        std::cout << ",\n  \"error\": \"" << JsonEscape(data.error) << "\"\n";
    } else {
        std::cout << "\n";
    }
    std::cout << "}\n";
    return data.valid ? 0 : 2;
}
