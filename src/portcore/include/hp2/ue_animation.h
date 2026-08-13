#pragma once

#include "hp2/ue_skeletal.h"

#include <cstddef>
#include <string>
#include <vector>

namespace hp2 {

struct Quaternion {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

struct BoneTransform {
    Quaternion rotation;
    Vec3 translation;
};

struct QuaternionKey {
    float time = 0.0f;
    Quaternion value;
};

struct PositionKey {
    float time = 0.0f;
    Vec3 value;
};

struct AnimationBoneTrack {
    std::vector<QuaternionKey> rotations;
    std::vector<PositionKey> positions;
};

struct CpuSkinInfluence {
    std::size_t bone_index = 0;
    float weight = 0.0f;
    Vec3 local_position;
};

Quaternion NormalizeQuaternion(const Quaternion& value);
Quaternion SlerpQuaternion(const Quaternion& a, const Quaternion& b, float alpha);
Vec3 RotateVector(const Quaternion& rotation, const Vec3& value);
BoneTransform ComposeBoneTransform(const BoneTransform& parent, const BoneTransform& local);

BoneTransform SampleBoneTrack(
    const AnimationBoneTrack& track,
    float time,
    const BoneTransform& fallback
);

std::vector<BoneTransform> MakeReferenceLocalPose(
    const std::vector<SkeletalReferenceBone>& bones
);

bool BuildModelSpacePose(
    const std::vector<SkeletalReferenceBone>& bones,
    const std::vector<BoneTransform>& local_pose,
    std::vector<BoneTransform>& model_pose,
    std::string* error = nullptr
);

std::vector<Vec3> CpuSkinPoints(
    const std::vector<std::vector<CpuSkinInfluence>>& influences,
    const std::vector<BoneTransform>& model_pose
);

}  // namespace hp2
