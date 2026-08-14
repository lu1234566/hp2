#pragma once

#include "hp2/ue_skeletal.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
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

struct HP2AnimationBone {
    std::string name;
    std::uint32_t flags = 0;
    std::int32_t parent_index = -1;
};

struct HP2AnimationTrack {
    std::uint32_t flags = 0;
    float position_scale = 0.0f;
    float time_scale = 0.0f;
    std::size_t delta_count = 0;
    AnimationBoneTrack keys;
};

struct HP2AnimationMove {
    Vec3 root_speed;
    float track_time = 0.0f;
    std::int32_t start_bone = 0;
    std::uint32_t flags = 0;
    std::vector<std::int32_t> bone_indices;
    std::vector<HP2AnimationTrack> tracks;
};

struct HP2AnimationData {
    bool valid = false;
    std::filesystem::path package_path;
    std::string package_name;
    std::string object_name;
    std::uint16_t file_version = 0;
    std::size_t master_quaternion_count = 0;
    std::size_t master_position_count = 0;
    std::size_t master_delta_count = 0;
    std::size_t total_track_count = 0;
    std::size_t remaining_bytes = 0;
    std::vector<HP2AnimationBone> bones;
    std::vector<SkeletalAnimationSequence> sequences;
    std::vector<HP2AnimationMove> moves;
    std::string error;
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

bool IsPlausibleDiagnosticPose(
    const std::vector<Vec3>& reference,
    const std::vector<Vec3>& candidate,
    float* deformation_score = nullptr
);

bool BuildCpuSkinInfluences(
    const SkeletalMeshSkinningData& skinning,
    std::vector<std::vector<CpuSkinInfluence>>& influences,
    std::string* error = nullptr
);

float AnimationMoveDuration(const HP2AnimationMove& move);

bool SampleAnimationMovePoints(
    const SkeletalMeshSkinningData& skinning,
    const HP2AnimationData& animation,
    std::size_t move_index,
    float time,
    const std::vector<std::vector<CpuSkinInfluence>>& influences,
    std::vector<Vec3>& points,
    std::string* error = nullptr
);

Quaternion DecodeHP2PackedQuaternion(
    std::int16_t raw_x,
    std::int16_t raw_y,
    std::int16_t raw_z,
    bool root_track
);

Vec3 DecodeHP2PackedPosition(
    std::int16_t raw_x,
    std::int16_t raw_y,
    std::int16_t raw_z,
    float position_scale
);

std::vector<float> DecodeHP2KeyDeltas(const std::vector<std::uint8_t>& deltas);

HP2AnimationData LoadHP2AnimationExport(
    const PackageIndex& package,
    std::size_t export_index
);

HP2AnimationData LoadNamedHP2Animation(
    const std::filesystem::path& game_root,
    const std::string& package_name,
    const std::string& object_name
);

}  // namespace hp2
