#include "hp2/ue_skeletal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_set>
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

struct IndexLaneStats {
    std::uint16_t low_min = std::numeric_limits<std::uint16_t>::max();
    std::uint16_t low_max = 0;
    std::uint16_t high_min = std::numeric_limits<std::uint16_t>::max();
    std::uint16_t high_max = 0;
    std::uint64_t low_sum = 0;
    std::uint64_t high_sum = 0;
    std::size_t second_nonzero = 0;
    std::size_t low_within_influences = 0;
    std::size_t high_within_influences = 0;
    std::size_t low_plus_high_within_influences = 0;
    std::uint32_t low_plus_high_max = 0;
    bool low_monotonic = true;
    bool high_monotonic = true;
    std::size_t low_unique = 0;
    std::size_t high_unique = 0;
    std::size_t interval_union_slots = 0;
    std::size_t interval_overlap_slots = 0;
    std::size_t interval_gap_slots = 0;
};

IndexLaneStats MeasureIndexLanes(const hp2::SkeletalMeshSkinningData& data) {
    IndexLaneStats stats;
    std::unordered_set<std::uint16_t> low_values;
    std::unordered_set<std::uint16_t> high_values;
    std::uint16_t previous_low = 0;
    std::uint16_t previous_high = 0;
    bool first = true;
    const std::size_t influence_count = data.weight_words.size();
    std::vector<std::uint16_t> coverage(influence_count, 0u);

    for (const auto& record : data.weight_indices) {
        const auto low = static_cast<std::uint16_t>(record.first & 0xffffu);
        const auto high = static_cast<std::uint16_t>((record.first >> 16u) & 0xffffu);
        stats.low_min = std::min(stats.low_min, low);
        stats.low_max = std::max(stats.low_max, low);
        stats.high_min = std::min(stats.high_min, high);
        stats.high_max = std::max(stats.high_max, high);
        stats.low_sum += low;
        stats.high_sum += high;
        stats.second_nonzero += record.second != 0u ? 1u : 0u;
        stats.low_within_influences += low <= influence_count ? 1u : 0u;
        stats.high_within_influences += high <= influence_count ? 1u : 0u;
        const std::uint32_t end_candidate = static_cast<std::uint32_t>(low)
            + static_cast<std::uint32_t>(high);
        stats.low_plus_high_max = std::max(stats.low_plus_high_max, end_candidate);
        stats.low_plus_high_within_influences += end_candidate <= influence_count ? 1u : 0u;
        if (end_candidate <= influence_count) {
            for (std::size_t slot = low; slot < end_candidate; ++slot) {
                ++coverage[slot];
            }
        }
        if (!first) {
            stats.low_monotonic = stats.low_monotonic && low >= previous_low;
            stats.high_monotonic = stats.high_monotonic && high >= previous_high;
        }
        previous_low = low;
        previous_high = high;
        first = false;
        low_values.insert(low);
        high_values.insert(high);
    }
    if (data.weight_indices.empty()) {
        stats.low_min = 0;
        stats.high_min = 0;
    }
    stats.low_unique = low_values.size();
    stats.high_unique = high_values.size();
    for (const auto count : coverage) {
        stats.interval_union_slots += count > 0u ? 1u : 0u;
        stats.interval_overlap_slots += count > 1u ? 1u : 0u;
        stats.interval_gap_slots += count == 0u ? 1u : 0u;
    }
    return stats;
}

struct WeightLaneStats {
    std::uint16_t low16_min = std::numeric_limits<std::uint16_t>::max();
    std::uint16_t low16_max = 0;
    std::uint16_t high16_min = std::numeric_limits<std::uint16_t>::max();
    std::uint16_t high16_max = 0;
    std::array<std::uint8_t, 4> byte_min{255u, 255u, 255u, 255u};
    std::array<std::uint8_t, 4> byte_max{0u, 0u, 0u, 0u};
    std::array<std::size_t, 4> byte_within_bones{};
    std::array<std::size_t, 4> byte_unique{};
    std::size_t low16_within_points = 0;
    std::size_t high16_within_points = 0;
    std::size_t low16_within_bones = 0;
    std::size_t high16_within_bones = 0;
    std::size_t low16_unique = 0;
    std::size_t high16_unique = 0;
    std::size_t raw_zero = 0;
    std::uint64_t high16_sum = 0;
    std::uint64_t point_weight_sum_min = 0;
    std::uint64_t point_weight_sum_max = 0;
    std::size_t point_weight_sum_65535 = 0;
    std::size_t point_weight_sum_65536 = 0;
    std::size_t point_weight_sum_other = 0;
    std::size_t points_with_weights = 0;
    std::size_t max_influences_per_point = 0;
};

WeightLaneStats MeasureWeightLanes(const hp2::SkeletalMeshSkinningData& data) {
    WeightLaneStats stats;
    std::unordered_set<std::uint16_t> low16_values;
    std::unordered_set<std::uint16_t> high16_values;
    std::array<std::unordered_set<std::uint8_t>, 4> byte_values;
    std::vector<std::uint64_t> point_weight_sums(data.reference_point_count, 0u);
    std::vector<std::size_t> point_influence_counts(data.reference_point_count, 0u);

    for (const auto& word : data.weight_words) {
        const std::uint16_t low16 = static_cast<std::uint16_t>(word.raw & 0xffffu);
        const std::uint16_t high16 = static_cast<std::uint16_t>((word.raw >> 16u) & 0xffffu);
        stats.low16_min = std::min(stats.low16_min, low16);
        stats.low16_max = std::max(stats.low16_max, low16);
        stats.high16_min = std::min(stats.high16_min, high16);
        stats.high16_max = std::max(stats.high16_max, high16);
        stats.low16_within_points += low16 < data.reference_point_count ? 1u : 0u;
        stats.high16_within_points += high16 < data.reference_point_count ? 1u : 0u;
        stats.low16_within_bones += low16 < data.bones.size() ? 1u : 0u;
        stats.high16_within_bones += high16 < data.bones.size() ? 1u : 0u;
        stats.raw_zero += word.raw == 0u ? 1u : 0u;
        stats.high16_sum += high16;
        if (low16 < data.reference_point_count) {
            point_weight_sums[low16] += high16;
            ++point_influence_counts[low16];
        }
        low16_values.insert(low16);
        high16_values.insert(high16);
        for (std::size_t lane = 0; lane < 4; ++lane) {
            const auto byte = static_cast<std::uint8_t>((word.raw >> (lane * 8u)) & 0xffu);
            stats.byte_min[lane] = std::min(stats.byte_min[lane], byte);
            stats.byte_max[lane] = std::max(stats.byte_max[lane], byte);
            stats.byte_within_bones[lane] += byte < data.bones.size() ? 1u : 0u;
            byte_values[lane].insert(byte);
        }
    }
    if (data.weight_words.empty()) {
        stats.low16_min = 0;
        stats.high16_min = 0;
        stats.byte_min = {0u, 0u, 0u, 0u};
    }
    stats.low16_unique = low16_values.size();
    stats.high16_unique = high16_values.size();
    for (std::size_t lane = 0; lane < 4; ++lane) {
        stats.byte_unique[lane] = byte_values[lane].size();
    }

    bool have_point = false;
    for (std::size_t index = 0; index < point_weight_sums.size(); ++index) {
        const std::size_t influences = point_influence_counts[index];
        if (influences == 0u) {
            continue;
        }
        const std::uint64_t sum = point_weight_sums[index];
        ++stats.points_with_weights;
        stats.max_influences_per_point = std::max(stats.max_influences_per_point, influences);
        if (!have_point) {
            stats.point_weight_sum_min = sum;
            stats.point_weight_sum_max = sum;
            have_point = true;
        } else {
            stats.point_weight_sum_min = std::min(stats.point_weight_sum_min, sum);
            stats.point_weight_sum_max = std::max(stats.point_weight_sum_max, sum);
        }
        stats.point_weight_sum_65535 += sum == 65535u ? 1u : 0u;
        stats.point_weight_sum_65536 += sum == 65536u ? 1u : 0u;
        stats.point_weight_sum_other += (sum != 65535u && sum != 65536u) ? 1u : 0u;
    }
    return stats;
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
    return Add(Add(
        Scale(u, 2.0 * dot_uv),
        Scale(value, q.w * q.w - dot_uu)
    ), Scale(cross, 2.0 * q.w));
}

Quat BoneQuat(const hp2::SkeletalReferenceBone& bone, bool wxyz, bool conjugate) {
    Quat value;
    if (wxyz) {
        value = {bone.orientation[1], bone.orientation[2], bone.orientation[3], bone.orientation[0]};
    } else {
        value = {bone.orientation[0], bone.orientation[1], bone.orientation[2], bone.orientation[3]};
    }
    value = Normalize(value);
    return conjugate ? Conjugate(value) : value;
}

bool ResolveHierarchyTransform(
    const hp2::SkeletalMeshSkinningData& data,
    std::size_t bone_index,
    bool wxyz,
    bool conjugate,
    bool reverse_compose,
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
    const Transform local{BoneQuat(bone, wxyz, conjugate), ToDVec3(bone.position)};
    const std::int32_t parent = bone.parent_index;
    if (parent < 0 || parent == static_cast<std::int32_t>(bone_index)) {
        transforms[bone_index] = local;
    } else {
        if (parent >= static_cast<std::int32_t>(data.bones.size())
            || !ResolveHierarchyTransform(
                data, static_cast<std::size_t>(parent), wxyz, conjugate,
                reverse_compose, transforms, states
            )) {
            return false;
        }
        const Transform& parent_transform = transforms[static_cast<std::size_t>(parent)];
        transforms[bone_index].rotation = reverse_compose
            ? Multiply(local.rotation, parent_transform.rotation)
            : Multiply(parent_transform.rotation, local.rotation);
        transforms[bone_index].position = Add(
            parent_transform.position,
            Rotate(parent_transform.rotation, local.position)
        );
    }
    states[bone_index] = 2u;
    return true;
}

struct BindCandidate {
    std::string name;
    bool valid = false;
    std::size_t compared_points = 0;
    double rms_error = 0.0;
    double max_error = 0.0;
};

BindCandidate EvaluateBindCandidate(
    const hp2::SkeletalMeshSkinningData& data,
    const std::string& name,
    bool hierarchy,
    bool wxyz,
    bool conjugate,
    bool reverse_compose
) {
    BindCandidate result;
    result.name = name;
    if (data.bones.empty() || data.reference_points.empty()
        || data.weight_words.size() != data.local_points.size()
        || data.weight_indices.size() != data.bones.size()) {
        return result;
    }

    std::vector<Transform> transforms(data.bones.size());
    if (hierarchy) {
        std::vector<std::uint8_t> states(data.bones.size(), 0u);
        for (std::size_t index = 0; index < data.bones.size(); ++index) {
            if (!ResolveHierarchyTransform(
                    data, index, wxyz, conjugate, reverse_compose, transforms, states)) {
                return result;
            }
        }
    } else {
        for (std::size_t index = 0; index < data.bones.size(); ++index) {
            transforms[index] = {
                BoneQuat(data.bones[index], wxyz, conjugate),
                ToDVec3(data.bones[index].position)
            };
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
    std::vector<std::uint64_t> weight_sums(data.reference_points.size(), 0u);
    for (std::size_t slot = 0; slot < data.weight_words.size(); ++slot) {
        const std::uint32_t packed = data.weight_words[slot].raw;
        const std::size_t point_index = packed & 0xffffu;
        const std::uint32_t weight = (packed >> 16u) & 0xffffu;
        const std::int32_t bone_index = influence_bone[slot];
        if (point_index >= accum.size() || bone_index < 0
            || static_cast<std::size_t>(bone_index) >= transforms.size()) {
            return result;
        }
        const Transform& transform = transforms[static_cast<std::size_t>(bone_index)];
        const DVec3 local = ToDVec3(data.local_points[slot]);
        const DVec3 placed = Add(transform.position, Rotate(transform.rotation, local));
        accum[point_index] = Add(accum[point_index], Scale(placed, static_cast<double>(weight)));
        weight_sums[point_index] += weight;
    }

    double squared_sum = 0.0;
    double max_squared = 0.0;
    for (std::size_t point_index = 0; point_index < data.reference_points.size(); ++point_index) {
        if (weight_sums[point_index] == 0u) {
            continue;
        }
        const DVec3 predicted = Scale(
            accum[point_index], 1.0 / static_cast<double>(weight_sums[point_index])
        );
        const DVec3 reference = ToDVec3(data.reference_points[point_index]);
        const double dx = predicted.x - reference.x;
        const double dy = predicted.y - reference.y;
        const double dz = predicted.z - reference.z;
        const double squared = dx * dx + dy * dy + dz * dz;
        squared_sum += squared;
        max_squared = std::max(max_squared, squared);
        ++result.compared_points;
    }
    if (result.compared_points == 0u) {
        return result;
    }
    result.rms_error = std::sqrt(squared_sum / static_cast<double>(result.compared_points));
    result.max_error = std::sqrt(max_squared);
    result.valid = std::isfinite(result.rms_error) && std::isfinite(result.max_error);
    return result;
}

std::vector<BindCandidate> EvaluateBindCandidates(const hp2::SkeletalMeshSkinningData& data) {
    std::vector<BindCandidate> candidates;
    const struct Variant {
        const char* name;
        bool hierarchy;
        bool wxyz;
        bool conjugate;
        bool reverse;
    } variants[] = {
        {"hier_xyzw", true, false, false, false},
        {"hier_xyzw_conjugate", true, false, true, false},
        {"hier_wxyz", true, true, false, false},
        {"hier_wxyz_conjugate", true, true, true, false},
        {"hier_reverse_xyzw", true, false, false, true},
        {"hier_reverse_xyzw_conjugate", true, false, true, true},
        {"hier_reverse_wxyz", true, true, false, true},
        {"hier_reverse_wxyz_conjugate", true, true, true, true},
        {"absolute_xyzw", false, false, false, false},
        {"absolute_xyzw_conjugate", false, false, true, false},
        {"absolute_wxyz", false, true, false, false},
        {"absolute_wxyz_conjugate", false, true, true, false},
    };
    for (const auto& variant : variants) {
        candidates.push_back(EvaluateBindCandidate(
            data, variant.name, variant.hierarchy, variant.wxyz,
            variant.conjugate, variant.reverse
        ));
    }
    return candidates;
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
    const IndexLaneStats index_lanes = MeasureIndexLanes(data);
    const WeightLaneStats weight_lanes = MeasureWeightLanes(data);
    const std::vector<BindCandidate> bind_candidates = EvaluateBindCandidates(data);
    const BindCandidate* best_bind = nullptr;
    for (const auto& candidate : bind_candidates) {
        if (candidate.valid && (best_bind == nullptr || candidate.rms_error < best_bind->rms_error)) {
            best_bind = &candidate;
        }
    }

    float min_weight = std::numeric_limits<float>::infinity();
    float max_weight = -std::numeric_limits<float>::infinity();
    for (const auto& word : data.weight_words) {
        if (!word.finite) {
            continue;
        }
        min_weight = std::min(min_weight, word.as_float);
        max_weight = std::max(max_weight, word.as_float);
    }
    const bool have_finite_weight = data.finite_weight_words > 0;

    std::cout << "{\n"
              << "  \"schema\": \"hp2-skeletal-probe-v4\",\n"
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
              << "  \"weight_index_second_nonzero\": " << index_lanes.second_nonzero << ",\n"
              << "  \"weight_index_low16_min\": " << index_lanes.low_min << ",\n"
              << "  \"weight_index_low16_max\": " << index_lanes.low_max << ",\n"
              << "  \"weight_index_low16_sum\": " << index_lanes.low_sum << ",\n"
              << "  \"weight_index_low16_unique\": " << index_lanes.low_unique << ",\n"
              << "  \"weight_index_low16_monotonic\": " << (index_lanes.low_monotonic ? "true" : "false") << ",\n"
              << "  \"weight_index_high16_min\": " << index_lanes.high_min << ",\n"
              << "  \"weight_index_high16_max\": " << index_lanes.high_max << ",\n"
              << "  \"weight_index_high16_sum\": " << index_lanes.high_sum << ",\n"
              << "  \"weight_index_high16_unique\": " << index_lanes.high_unique << ",\n"
              << "  \"weight_index_high16_monotonic\": " << (index_lanes.high_monotonic ? "true" : "false") << ",\n"
              << "  \"weight_index_low16_within_influences\": " << index_lanes.low_within_influences << ",\n"
              << "  \"weight_index_high16_within_influences\": " << index_lanes.high_within_influences << ",\n"
              << "  \"weight_index_low_plus_high_max\": " << index_lanes.low_plus_high_max << ",\n"
              << "  \"weight_index_low_plus_high_within_influences\": " << index_lanes.low_plus_high_within_influences << ",\n"
              << "  \"weight_index_interval_union_slots\": " << index_lanes.interval_union_slots << ",\n"
              << "  \"weight_index_interval_overlap_slots\": " << index_lanes.interval_overlap_slots << ",\n"
              << "  \"weight_index_interval_gap_slots\": " << index_lanes.interval_gap_slots << ",\n"
              << "  \"weight_words\": " << data.weight_words.size() << ",\n"
              << "  \"weight_word_raw_zero\": " << weight_lanes.raw_zero << ",\n"
              << "  \"weight_word_low16_min\": " << weight_lanes.low16_min << ",\n"
              << "  \"weight_word_low16_max\": " << weight_lanes.low16_max << ",\n"
              << "  \"weight_word_low16_unique\": " << weight_lanes.low16_unique << ",\n"
              << "  \"weight_word_low16_within_points\": " << weight_lanes.low16_within_points << ",\n"
              << "  \"weight_word_low16_within_bones\": " << weight_lanes.low16_within_bones << ",\n"
              << "  \"weight_word_high16_min\": " << weight_lanes.high16_min << ",\n"
              << "  \"weight_word_high16_max\": " << weight_lanes.high16_max << ",\n"
              << "  \"weight_word_high16_unique\": " << weight_lanes.high16_unique << ",\n"
              << "  \"weight_word_high16_within_points\": " << weight_lanes.high16_within_points << ",\n"
              << "  \"weight_word_high16_within_bones\": " << weight_lanes.high16_within_bones << ",\n"
              << "  \"weight_word_high16_sum\": " << weight_lanes.high16_sum << ",\n"
              << "  \"weight_word_byte_min\": ["
              << static_cast<unsigned>(weight_lanes.byte_min[0]) << ", "
              << static_cast<unsigned>(weight_lanes.byte_min[1]) << ", "
              << static_cast<unsigned>(weight_lanes.byte_min[2]) << ", "
              << static_cast<unsigned>(weight_lanes.byte_min[3]) << "],\n"
              << "  \"weight_word_byte_max\": ["
              << static_cast<unsigned>(weight_lanes.byte_max[0]) << ", "
              << static_cast<unsigned>(weight_lanes.byte_max[1]) << ", "
              << static_cast<unsigned>(weight_lanes.byte_max[2]) << ", "
              << static_cast<unsigned>(weight_lanes.byte_max[3]) << "],\n"
              << "  \"weight_word_byte_unique\": ["
              << weight_lanes.byte_unique[0] << ", " << weight_lanes.byte_unique[1] << ", "
              << weight_lanes.byte_unique[2] << ", " << weight_lanes.byte_unique[3] << "],\n"
              << "  \"weight_word_byte_within_bones\": ["
              << weight_lanes.byte_within_bones[0] << ", " << weight_lanes.byte_within_bones[1] << ", "
              << weight_lanes.byte_within_bones[2] << ", " << weight_lanes.byte_within_bones[3] << "],\n"
              << "  \"points_with_weight_records\": " << weight_lanes.points_with_weights << ",\n"
              << "  \"max_influences_per_point\": " << weight_lanes.max_influences_per_point << ",\n"
              << "  \"point_weight_sum_min\": " << weight_lanes.point_weight_sum_min << ",\n"
              << "  \"point_weight_sum_max\": " << weight_lanes.point_weight_sum_max << ",\n"
              << "  \"point_weight_sum_65535\": " << weight_lanes.point_weight_sum_65535 << ",\n"
              << "  \"point_weight_sum_65536\": " << weight_lanes.point_weight_sum_65536 << ",\n"
              << "  \"point_weight_sum_other\": " << weight_lanes.point_weight_sum_other << ",\n"
              << "  \"bind_candidate_count\": " << bind_candidates.size() << ",\n"
              << "  \"best_bind_candidate\": ";
    if (best_bind != nullptr) {
        std::cout << "\"" << JsonEscape(best_bind->name) << "\",\n"
                  << "  \"best_bind_rms_error\": " << best_bind->rms_error << ",\n"
                  << "  \"best_bind_max_error\": " << best_bind->max_error << ",\n"
                  << "  \"best_bind_points\": " << best_bind->compared_points << ",\n";
    } else {
        std::cout << "null,\n"
                  << "  \"best_bind_rms_error\": null,\n"
                  << "  \"best_bind_max_error\": null,\n"
                  << "  \"best_bind_points\": 0,\n";
    }
    std::cout << "  \"bind_candidates\": [\n";
    for (std::size_t index = 0; index < bind_candidates.size(); ++index) {
        const auto& candidate = bind_candidates[index];
        std::cout << "    {\"name\": \"" << JsonEscape(candidate.name)
                  << "\", \"valid\": " << (candidate.valid ? "true" : "false")
                  << ", \"points\": " << candidate.compared_points
                  << ", \"rms_error\": ";
        if (candidate.valid) {
            std::cout << candidate.rms_error;
        } else {
            std::cout << "null";
        }
        std::cout << ", \"max_error\": ";
        if (candidate.valid) {
            std::cout << candidate.max_error;
        } else {
            std::cout << "null";
        }
        std::cout << "}" << (index + 1 == bind_candidates.size() ? "" : ",") << '\n';
    }
    std::cout << "  ],\n"
              << "  \"finite_weight_words\": " << data.finite_weight_words << ",\n"
              << "  \"unit_interval_weight_words\": " << data.unit_interval_weight_words << ",\n"
              << "  \"nonfinite_weight_words\": " << data.nonfinite_weight_words << ",\n"
              << "  \"finite_weight_min\": ";
    if (have_finite_weight) {
        std::cout << min_weight;
    } else {
        std::cout << "null";
    }
    std::cout << ",\n  \"finite_weight_max\": ";
    if (have_finite_weight) {
        std::cout << max_weight;
    } else {
        std::cout << "null";
    }
    std::cout << ",\n"
              << "  \"local_points\": " << data.local_points.size() << ",\n"
              << "  \"remaining_bytes\": " << data.remaining_bytes << ",\n"
              << "  \"sequences\": [\n";
    for (std::size_t index = 0; index < data.sequences.size(); ++index) {
        const auto& sequence = data.sequences[index];
        std::cout << "    {\"name\": \"" << JsonEscape(sequence.name)
                  << "\", \"group\": \"" << JsonEscape(sequence.group)
                  << "\", \"start_frame\": " << sequence.start_frame
                  << ", \"frame_count\": " << sequence.frame_count
                  << ", \"notify_count\": " << sequence.notify_count
                  << ", \"rate\": " << sequence.rate << "}"
                  << (index + 1 == data.sequences.size() ? "" : ",") << '\n';
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
