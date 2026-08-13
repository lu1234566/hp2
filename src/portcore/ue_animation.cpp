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
                bone.orientation[0], bone.orientation[1], bone.orientation[2], bone.orientation[3]
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

}  // namespace hp2
