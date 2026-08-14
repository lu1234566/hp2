#include "hp2/ue_animation.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace hp2 {
namespace {

constexpr float kEpsilon = 1.0e-6f;

float Clamp01(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

Vec3 Add(const Vec3& a, const Vec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 Scale(const Vec3& value, float scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}

Vec3 Lerp(const Vec3& a, const Vec3& b, float alpha) {
    const float t = Clamp01(alpha);
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
    };
}

struct PointBounds {
    bool valid = false;
    Vec3 minimum;
    Vec3 maximum;
    Vec3 center;
    float spans[3]{};
    float extent = 0.0f;
    std::size_t major_axis = 0u;
};

PointBounds MeasurePointBounds(const std::vector<Vec3>& points) {
    PointBounds result;
    for (const Vec3& point : points) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) {
            return {};
        }
        if (!result.valid) {
            result.minimum = point;
            result.maximum = point;
            result.valid = true;
        } else {
            result.minimum.x = std::min(result.minimum.x, point.x);
            result.minimum.y = std::min(result.minimum.y, point.y);
            result.minimum.z = std::min(result.minimum.z, point.z);
            result.maximum.x = std::max(result.maximum.x, point.x);
            result.maximum.y = std::max(result.maximum.y, point.y);
            result.maximum.z = std::max(result.maximum.z, point.z);
        }
    }
    if (!result.valid) return result;
    result.center = {
        (result.minimum.x + result.maximum.x) * 0.5f,
        (result.minimum.y + result.maximum.y) * 0.5f,
        (result.minimum.z + result.maximum.z) * 0.5f,
    };
    result.spans[0] = result.maximum.x - result.minimum.x;
    result.spans[1] = result.maximum.y - result.minimum.y;
    result.spans[2] = result.maximum.z - result.minimum.z;
    result.major_axis = static_cast<std::size_t>(
        std::distance(result.spans, std::max_element(result.spans, result.spans + 3))
    );
    result.extent = result.spans[result.major_axis];
    result.valid = std::isfinite(result.extent) && result.extent > 0.0f;
    return result;
}

template <typename Key>
std::size_t UpperKeyIndex(const std::vector<Key>& keys, float time) {
    const auto it = std::upper_bound(
        keys.begin(), keys.end(), time,
        [](float sample_time, const Key& key) { return sample_time < key.time; }
    );
    return static_cast<std::size_t>(std::distance(keys.begin(), it));
}

Quaternion SampleRotation(
    const std::vector<QuaternionKey>& keys,
    float time,
    const Quaternion& fallback
) {
    if (keys.empty()) return NormalizeQuaternion(fallback);
    if (keys.size() == 1 || time <= keys.front().time) {
        return NormalizeQuaternion(keys.front().value);
    }
    if (time >= keys.back().time) {
        return NormalizeQuaternion(keys.back().value);
    }
    const std::size_t upper = UpperKeyIndex(keys, time);
    const std::size_t lower = upper - 1u;
    const float span = keys[upper].time - keys[lower].time;
    const float alpha = span > kEpsilon ? (time - keys[lower].time) / span : 0.0f;
    return SlerpQuaternion(keys[lower].value, keys[upper].value, alpha);
}

Vec3 SamplePosition(
    const std::vector<PositionKey>& keys,
    float time,
    const Vec3& fallback
) {
    if (keys.empty()) return fallback;
    if (keys.size() == 1 || time <= keys.front().time) return keys.front().value;
    if (time >= keys.back().time) return keys.back().value;
    const std::size_t upper = UpperKeyIndex(keys, time);
    const std::size_t lower = upper - 1u;
    const float span = keys[upper].time - keys[lower].time;
    const float alpha = span > kEpsilon ? (time - keys[lower].time) / span : 0.0f;
    return Lerp(keys[lower].value, keys[upper].value, alpha);
}

}  // namespace

Quaternion NormalizeQuaternion(const Quaternion& value) {
    const float length_squared = value.x * value.x + value.y * value.y
        + value.z * value.z + value.w * value.w;
    if (!std::isfinite(length_squared) || length_squared <= kEpsilon * kEpsilon) {
        return {};
    }
    const float inverse_length = 1.0f / std::sqrt(length_squared);
    return {
        value.x * inverse_length,
        value.y * inverse_length,
        value.z * inverse_length,
        value.w * inverse_length,
    };
}

Quaternion SlerpQuaternion(const Quaternion& a_value, const Quaternion& b_value, float alpha) {
    Quaternion a = NormalizeQuaternion(a_value);
    Quaternion b = NormalizeQuaternion(b_value);
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (dot < 0.0f) {
        b.x = -b.x;
        b.y = -b.y;
        b.z = -b.z;
        b.w = -b.w;
        dot = -dot;
    }
    dot = std::max(-1.0f, std::min(1.0f, dot));
    const float t = Clamp01(alpha);
    if (dot > 0.9995f) {
        return NormalizeQuaternion({
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t,
        });
    }
    const float theta = std::acos(dot);
    const float sin_theta = std::sin(theta);
    if (std::fabs(sin_theta) <= kEpsilon) return a;
    const float wa = std::sin((1.0f - t) * theta) / sin_theta;
    const float wb = std::sin(t * theta) / sin_theta;
    return NormalizeQuaternion({
        a.x * wa + b.x * wb,
        a.y * wa + b.y * wb,
        a.z * wa + b.z * wb,
        a.w * wa + b.w * wb,
    });
}

Vec3 RotateVector(const Quaternion& rotation_value, const Vec3& value) {
    const Quaternion q = NormalizeQuaternion(rotation_value);
    const Vec3 u{q.x, q.y, q.z};
    const float dot_uv = u.x * value.x + u.y * value.y + u.z * value.z;
    const float dot_uu = u.x * u.x + u.y * u.y + u.z * u.z;
    const Vec3 cross{
        u.y * value.z - u.z * value.y,
        u.z * value.x - u.x * value.z,
        u.x * value.y - u.y * value.x,
    };
    return Add(
        Add(Scale(u, 2.0f * dot_uv), Scale(value, q.w * q.w - dot_uu)),
        Scale(cross, 2.0f * q.w)
    );
}

BoneTransform ComposeBoneTransform(const BoneTransform& parent, const BoneTransform& local) {
    const Quaternion p = NormalizeQuaternion(parent.rotation);
    const Quaternion l = NormalizeQuaternion(local.rotation);
    const Quaternion combined{
        p.w * l.x + p.x * l.w + p.y * l.z - p.z * l.y,
        p.w * l.y - p.x * l.z + p.y * l.w + p.z * l.x,
        p.w * l.z + p.x * l.y - p.y * l.x + p.z * l.w,
        p.w * l.w - p.x * l.x - p.y * l.y - p.z * l.z,
    };
    return {
        NormalizeQuaternion(combined),
        Add(parent.translation, RotateVector(p, local.translation)),
    };
}

BoneTransform SampleBoneTrack(
    const AnimationBoneTrack& track,
    float time,
    const BoneTransform& fallback
) {
    return {
        SampleRotation(track.rotations, time, fallback.rotation),
        SamplePosition(track.positions, time, fallback.translation),
    };
}

std::vector<BoneTransform> MakeReferenceLocalPose(
    const std::vector<SkeletalReferenceBone>& bones
) {
    std::vector<BoneTransform> result;
    result.reserve(bones.size());
    for (const auto& bone : bones) {
        result.push_back({
            NormalizeQuaternion({
                -bone.orientation[0], -bone.orientation[1], -bone.orientation[2],
                bone.orientation[3]
            }),
            bone.position,
        });
    }
    return result;
}

bool BuildModelSpacePose(
    const std::vector<SkeletalReferenceBone>& bones,
    const std::vector<BoneTransform>& local_pose,
    std::vector<BoneTransform>& model_pose,
    std::string* error
) {
    model_pose.clear();
    if (bones.size() != local_pose.size()) {
        if (error) *error = "bone count does not match local pose";
        return false;
    }
    model_pose.resize(bones.size());
    std::vector<std::uint8_t> state(bones.size(), 0u);

    std::function<bool(std::size_t)> resolve = [&](std::size_t index) {
        if (state[index] == 2u) return true;
        if (state[index] == 1u) {
            if (error) *error = "cycle in skeletal hierarchy";
            return false;
        }
        state[index] = 1u;
        const std::int32_t parent_index = bones[index].parent_index;
        if (parent_index < 0 || static_cast<std::size_t>(parent_index) == index) {
            model_pose[index] = local_pose[index];
            model_pose[index].rotation = NormalizeQuaternion(model_pose[index].rotation);
        } else {
            if (static_cast<std::size_t>(parent_index) >= bones.size()) {
                if (error) *error = "parent bone index is outside skeleton";
                return false;
            }
            if (!resolve(static_cast<std::size_t>(parent_index))) return false;
            model_pose[index] = ComposeBoneTransform(
                model_pose[static_cast<std::size_t>(parent_index)], local_pose[index]
            );
        }
        state[index] = 2u;
        return true;
    };

    for (std::size_t index = 0; index < bones.size(); ++index) {
        if (!resolve(index)) {
            model_pose.clear();
            return false;
        }
    }
    if (error) error->clear();
    return true;
}

std::vector<Vec3> CpuSkinPoints(
    const std::vector<std::vector<CpuSkinInfluence>>& influences,
    const std::vector<BoneTransform>& model_pose
) {
    std::vector<Vec3> result(influences.size());
    for (std::size_t point_index = 0; point_index < influences.size(); ++point_index) {
        Vec3 accumulated{};
        float total_weight = 0.0f;
        for (const auto& influence : influences[point_index]) {
            if (influence.bone_index >= model_pose.size() || !std::isfinite(influence.weight)
                || influence.weight <= 0.0f) {
                continue;
            }
            const auto& bone = model_pose[influence.bone_index];
            const Vec3 transformed = Add(
                bone.translation,
                RotateVector(bone.rotation, influence.local_position)
            );
            accumulated = Add(accumulated, Scale(transformed, influence.weight));
            total_weight += influence.weight;
        }
        if (total_weight > kEpsilon) {
            result[point_index] = Scale(accumulated, 1.0f / total_weight);
        }
    }
    return result;
}

bool IsPlausibleDiagnosticPose(
    const std::vector<Vec3>& reference,
    const std::vector<Vec3>& candidate,
    float* deformation_score
) {
    if (reference.size() != candidate.size() || reference.empty()) return false;
    const PointBounds reference_bounds = MeasurePointBounds(reference);
    const PointBounds candidate_bounds = MeasurePointBounds(candidate);
    if (!reference_bounds.valid || !candidate_bounds.valid) return false;
    const float extent_ratio = candidate_bounds.extent / reference_bounds.extent;
    if (!std::isfinite(extent_ratio) || extent_ratio < 0.25f || extent_ratio > 4.0f) {
        return false;
    }
    const std::size_t upright_axis = reference_bounds.major_axis;
    if (candidate_bounds.spans[upright_axis]
            < reference_bounds.spans[upright_axis] * 0.55f
        || candidate_bounds.spans[upright_axis] < candidate_bounds.extent * 0.70f) {
        return false;
    }
    const Vec3 center_delta = {
        candidate_bounds.center.x - reference_bounds.center.x,
        candidate_bounds.center.y - reference_bounds.center.y,
        candidate_bounds.center.z - reference_bounds.center.z,
    };
    const float center_distance = std::sqrt(
        center_delta.x * center_delta.x + center_delta.y * center_delta.y
        + center_delta.z * center_delta.z
    );
    if (!std::isfinite(center_distance) || center_distance > reference_bounds.extent * 2.0f) {
        return false;
    }
    double squared = 0.0;
    for (std::size_t index = 0; index < reference.size(); ++index) {
        const double x = static_cast<double>(candidate[index].x - reference[index].x)
            - center_delta.x;
        const double y = static_cast<double>(candidate[index].y - reference[index].y)
            - center_delta.y;
        const double z = static_cast<double>(candidate[index].z - reference[index].z)
            - center_delta.z;
        squared += x * x + y * y + z * z;
    }
    const float score = static_cast<float>(
        std::sqrt(squared / static_cast<double>(reference.size())) / reference_bounds.extent
    );
    if (!std::isfinite(score) || score > 2.0f) return false;
    if (deformation_score) *deformation_score = score;
    return true;
}

bool BuildCpuSkinInfluences(
    const SkeletalMeshSkinningData& skinning,
    std::vector<std::vector<CpuSkinInfluence>>& influences,
    std::string* error
) {
    influences.clear();
    auto fail = [&](const char* message) {
        if (error) *error = message;
        return false;
    };
    if (!skinning.valid) return fail("skeletal skinning stream is invalid");
    if (skinning.reference_points.empty() || skinning.bones.empty()) {
        return fail("skeletal skinning stream has no points or bones");
    }
    if (skinning.weight_indices.size() != skinning.bones.size()) {
        return fail("weight-index record count does not match bone count");
    }
    if (skinning.weight_words.size() != skinning.local_points.size()) {
        return fail("weight words and local points do not have matching slots");
    }

    influences.resize(skinning.reference_points.size());
    std::vector<std::uint8_t> claimed(skinning.weight_words.size(), 0u);
    for (std::size_t bone_index = 0; bone_index < skinning.weight_indices.size(); ++bone_index) {
        const SkeletalWeightIndexRecord& record = skinning.weight_indices[bone_index];
        if (record.second != 0u) {
            influences.clear();
            return fail("unsupported nonzero second weight-index word");
        }
        const std::size_t first_slot = static_cast<std::size_t>(record.first & 0xffffu);
        const std::size_t slot_count = static_cast<std::size_t>(record.first >> 16u);
        if (first_slot > skinning.weight_words.size()
            || slot_count > skinning.weight_words.size() - first_slot) {
            influences.clear();
            return fail("packed weight-index range is outside the influence slots");
        }
        for (std::size_t offset = 0; offset < slot_count; ++offset) {
            const std::size_t slot = first_slot + offset;
            if (claimed[slot] != 0u) {
                influences.clear();
                return fail("packed weight-index ranges overlap");
            }
            claimed[slot] = 1u;
            const std::uint32_t packed = skinning.weight_words[slot].raw;
            const std::size_t point_index = static_cast<std::size_t>(packed & 0xffffu);
            const std::uint32_t raw_weight = packed >> 16u;
            if (point_index >= influences.size()) {
                influences.clear();
                return fail("packed influence has an invalid point index");
            }
            if (raw_weight == 0u) continue;
            influences[point_index].push_back({
                bone_index,
                static_cast<float>(raw_weight) / 65535.0f,
                skinning.local_points[slot],
            });
        }
    }
    if (std::find(claimed.begin(), claimed.end(), 0u) != claimed.end()) {
        influences.clear();
        return fail("packed weight-index ranges leave unused influence slots");
    }
    for (const auto& point_influences : influences) {
        if (point_influences.empty()) {
            influences.clear();
            return fail("reference point has no skeletal influence");
        }
    }
    if (error) error->clear();
    return true;
}

float AnimationMoveDuration(const HP2AnimationMove& move) {
    float duration = std::isfinite(move.track_time) && move.track_time > 0.0f
        ? move.track_time : 0.0f;
    for (const HP2AnimationTrack& track : move.tracks) {
        if (!track.keys.rotations.empty()) {
            duration = std::max(duration, track.keys.rotations.back().time);
        }
        if (!track.keys.positions.empty()) {
            duration = std::max(duration, track.keys.positions.back().time);
        }
    }
    return std::isfinite(duration) ? duration : 0.0f;
}

bool SampleAnimationMovePoints(
    const SkeletalMeshSkinningData& skinning,
    const HP2AnimationData& animation,
    std::size_t move_index,
    float time,
    const std::vector<std::vector<CpuSkinInfluence>>& influences,
    std::vector<Vec3>& points,
    std::string* error
) {
    points.clear();
    auto fail = [&](const char* message) {
        if (error) *error = message;
        return false;
    };
    if (!skinning.valid || !animation.valid) {
        return fail("skeletal mesh or animation stream is invalid");
    }
    if (move_index >= animation.moves.size()) {
        return fail("animation move index is outside the move table");
    }
    if (skinning.bones.size() != animation.bones.size()) {
        return fail("animation and mesh skeletons have different bone counts");
    }
    if (influences.size() != skinning.reference_points.size()) {
        return fail("CPU influence table does not match the reference-point count");
    }

    const HP2AnimationMove& move = animation.moves[move_index];
    std::vector<BoneTransform> local_pose = MakeReferenceLocalPose(skinning.bones);
    for (std::size_t track_index = 0; track_index < move.tracks.size(); ++track_index) {
        std::int32_t bone_index = move.start_bone + static_cast<std::int32_t>(track_index);
        if (track_index < move.bone_indices.size()) {
            bone_index = move.bone_indices[track_index];
        }
        if (bone_index < 0 || static_cast<std::size_t>(bone_index) >= local_pose.size()) {
            return fail("animation track maps outside the mesh skeleton");
        }
        local_pose[static_cast<std::size_t>(bone_index)] = SampleBoneTrack(
            move.tracks[track_index].keys,
            std::isfinite(time) ? std::max(time, 0.0f) : 0.0f,
            local_pose[static_cast<std::size_t>(bone_index)]
        );
    }

    std::vector<BoneTransform> model_pose;
    std::string pose_error;
    if (!BuildModelSpacePose(skinning.bones, local_pose, model_pose, &pose_error)) {
        if (error) *error = pose_error;
        return false;
    }
    points = CpuSkinPoints(influences, model_pose);
    if (points.size() != skinning.reference_points.size()) {
        points.clear();
        return fail("CPU skinning produced an unexpected point count");
    }
    for (const Vec3& point : points) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) {
            points.clear();
            return fail("CPU skinning produced a non-finite point");
        }
    }
    if (error) error->clear();
    return true;
}

}  // namespace hp2
