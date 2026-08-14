#pragma once

#include "hp2/ue_actor.h"
#include "hp2/ue_package.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace hp2 {

struct SkeletalAnimationSequence {
    std::string name;
    std::string group;
    std::int32_t start_frame = 0;
    std::int32_t frame_count = 0;
    std::size_t notify_count = 0;
    float rate = 0.0f;
};

struct SkeletalReferenceBone {
    std::string name;
    std::uint32_t flags = 0;
    std::array<float, 4> orientation{};
    Vec3 position;
    float length = 0.0f;
    Vec3 size;
    std::uint32_t child_count = 0;
    std::int32_t parent_index = -1;
};

struct SkeletalWeightIndexRecord {
    std::uint32_t first = 0;
    std::uint32_t second = 0;
};

struct SkeletalWeightWord {
    std::uint32_t raw = 0;
    float as_float = 0.0f;
    bool finite = false;
};

struct SkeletalMeshSkinningData {
    bool valid = false;
    std::filesystem::path package_path;
    std::string package_name;
    std::string object_name;
    std::uint16_t file_version = 0;
    std::int32_t frame_vertices = 0;
    std::int32_t animation_frames = 0;
    std::size_t reference_point_count = 0;
    std::size_t root_like_bones = 0;
    std::size_t invalid_parent_bones = 0;
    std::size_t finite_weight_words = 0;
    std::size_t unit_interval_weight_words = 0;
    std::size_t nonfinite_weight_words = 0;
    std::uint32_t weight_index_first_max = 0;
    std::uint32_t weight_index_second_max = 0;
    std::int32_t animation_reference = 0;
    std::string animation_object_name;
    std::size_t remaining_bytes = 0;
    std::vector<SkeletalAnimationSequence> sequences;
    std::vector<Vec3> reference_points;
    std::vector<SkeletalReferenceBone> bones;
    std::vector<SkeletalWeightIndexRecord> weight_indices;
    std::vector<SkeletalWeightWord> weight_words;
    std::vector<Vec3> local_points;
    std::string error;
};

SkeletalMeshSkinningData LoadSkeletalMeshSkinningExport(
    const PackageIndex& package,
    std::size_t export_index
);

SkeletalMeshSkinningData LoadNamedSkeletalMeshSkinning(
    const std::filesystem::path& game_root,
    const std::string& package_name,
    const std::string& object_name
);

}  // namespace hp2
