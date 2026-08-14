#include "hp2/ue_animation_hp2.h"

#include <cstdint>
#include <cstring>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {
bool Near(float a, float b) { return std::fabs(a - b) < 0.001f; }
bool Near(const hp2::Vec3& a, const hp2::Vec3& b) {
    return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z);
}
hp2::Quaternion Z90() {
    constexpr float k = 0.70710678118f;
    return {0.0f, 0.0f, k, k};
}

void AppendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (std::uint32_t shift = 0; shift < 32u; shift += 8u) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}

void AppendI32(std::vector<std::uint8_t>& bytes, std::int32_t value) {
    AppendU32(bytes, static_cast<std::uint32_t>(value));
}

void AppendI16(std::vector<std::uint8_t>& bytes, std::int16_t value) {
    const auto raw = static_cast<std::uint16_t>(value);
    bytes.push_back(static_cast<std::uint8_t>(raw & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((raw >> 8u) & 0xffu));
}

void AppendF32(std::vector<std::uint8_t>& bytes, float value) {
    std::uint32_t raw = 0u;
    static_assert(sizeof(raw) == sizeof(value));
    std::memcpy(&raw, &value, sizeof(raw));
    AppendU32(bytes, raw);
}

void AppendCompact(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    std::uint8_t first = static_cast<std::uint8_t>(value & 0x3fu);
    value >>= 6u;
    if (value != 0u) first |= 0x40u;
    bytes.push_back(first);
    while (value != 0u) {
        std::uint8_t next = static_cast<std::uint8_t>(value & 0x7fu);
        value >>= 7u;
        if (value != 0u) next |= 0x80u;
        bytes.push_back(next);
    }
}

std::vector<std::uint8_t> SyntheticAnimationNative() {
    std::vector<std::uint8_t> bytes;
    AppendCompact(bytes, 2u);
    AppendCompact(bytes, 0u);
    AppendU32(bytes, 0u);
    AppendI32(bytes, 0);
    AppendCompact(bytes, 1u);
    AppendU32(bytes, 0u);
    AppendI32(bytes, 0);

    AppendCompact(bytes, 1u);
    AppendF32(bytes, 0.0f);
    AppendF32(bytes, 0.0f);
    AppendF32(bytes, 0.0f);
    AppendF32(bytes, 1.25f);
    AppendI32(bytes, 0);
    AppendU32(bytes, 0u);
    AppendCompact(bytes, 2u);
    AppendI32(bytes, 0);
    AppendI32(bytes, 1);
    AppendCompact(bytes, 2u);

    AppendU32(bytes, 0u);
    AppendCompact(bytes, 2u);
    AppendCompact(bytes, 1u);
    AppendCompact(bytes, 2u);
    AppendF32(bytes, 10.0f);
    AppendF32(bytes, 0.5f);

    AppendU32(bytes, 0u);
    AppendCompact(bytes, 2u);
    AppendCompact(bytes, 2u);
    AppendCompact(bytes, 2u);
    AppendF32(bytes, 4.0f);
    AppendF32(bytes, 0.25f);

    AppendCompact(bytes, 1u);
    AppendCompact(bytes, 2u);
    AppendCompact(bytes, 3u);
    AppendI32(bytes, 0);
    AppendI32(bytes, 3);
    AppendCompact(bytes, 0u);
    AppendF32(bytes, 30.0f);

    AppendCompact(bytes, 4u);
    AppendI16(bytes, 0);
    AppendI16(bytes, 0);
    AppendI16(bytes, 0);
    AppendI16(bytes, 0);
    AppendI16(bytes, 0);
    AppendI16(bytes, 16384);
    AppendI16(bytes, 0);
    AppendI16(bytes, 0);
    AppendI16(bytes, 0);
    AppendI16(bytes, 8192);
    AppendI16(bytes, 0);
    AppendI16(bytes, 0);

    AppendCompact(bytes, 3u);
    AppendI16(bytes, 32767);
    AppendI16(bytes, 0);
    AppendI16(bytes, 0);
    AppendI16(bytes, 0);
    AppendI16(bytes, 16384);
    AppendI16(bytes, 0);
    AppendI16(bytes, 0);
    AppendI16(bytes, 0);
    AppendI16(bytes, 32767);

    AppendCompact(bytes, 4u);
    bytes.insert(bytes.end(), {0u, 2u, 0u, 4u});
    return bytes;
}
}

int main() {
    const hp2::Vec3 rotated = hp2::RotateVector(Z90(), {1.0f, 0.0f, 0.0f});
    if (!Near(rotated, {0.0f, 1.0f, 0.0f})) return 1;

    std::vector<hp2::SkeletalReferenceBone> bones(2);
    bones[0].parent_index = -1;
    bones[1].parent_index = 0;
    std::vector<hp2::BoneTransform> local(2);
    local[0] = {Z90(), {10.0f, 0.0f, 0.0f}};
    local[1] = {{}, {1.0f, 0.0f, 0.0f}};
    std::vector<hp2::BoneTransform> model;
    if (!hp2::BuildModelSpacePose(bones, local, model)) return 2;
    if (model.size() != 2 || !Near(model[1].translation, {10.0f, 1.0f, 0.0f})) return 3;

    const std::vector<std::vector<hp2::CpuSkinInfluence>> influences{{
        {0u, 0.5f, {1.0f, 0.0f, 0.0f}},
        {1u, 0.5f, {1.0f, 0.0f, 0.0f}}
    }};
    const std::vector<hp2::BoneTransform> pose{{{}, {}}, {{}, {2.0f, 0.0f, 0.0f}}};
    const auto points = hp2::CpuSkinPoints(influences, pose);
    if (points.size() != 1 || !Near(points[0], {2.0f, 0.0f, 0.0f})) return 4;

    const hp2::Quaternion packed_root = hp2::DecodeHP2PackedQuaternion(
        0, 16384, 0, true
    );
    const hp2::Quaternion packed_child = hp2::DecodeHP2PackedQuaternion(
        0, 16384, 0, false
    );
    const hp2::Vec3 packed_position = hp2::DecodeHP2PackedPosition(0, 16384, 0, 4.0f);
    if (packed_root.y <= 0.0f || packed_root.w <= 0.0f
        || packed_child.y <= 0.0f || packed_child.w >= 0.0f
        || !Near(packed_position, {0.0f, 2.0f, 0.0f})) return 5;

    const std::vector<hp2::Vec3> upright_reference{
        {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 10.0f}
    };
    const std::vector<hp2::Vec3> sideways_candidate{
        {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f}, {10.0f, 0.0f, 0.0f}
    };
    float upright_deformation = -1.0f;
    if (!hp2::IsPlausibleDiagnosticPose(
            upright_reference, upright_reference, &upright_deformation
        ) || !Near(upright_deformation, 0.0f)
        || hp2::IsPlausibleDiagnosticPose(upright_reference, sideways_candidate)) return 6;

    hp2::SkeletalMeshSkinningData skinning;
    skinning.valid = true;
    skinning.reference_points = {{11.0f, 0.0f, 0.0f}};
    skinning.reference_point_count = 1u;
    skinning.bones.resize(1u);
    skinning.bones[0].orientation = {0.0f, 0.0f, Z90().z, Z90().w};
    skinning.bones[0].position = {10.0f, 0.0f, 0.0f};
    skinning.bones[0].parent_index = -1;
    skinning.weight_indices = {{2u << 16u, 0u}};
    skinning.weight_words = {
        {0x00000000u, 0.0f, true},
        {0xffff0000u, 0.0f, true},
    };
    skinning.local_points = {
        {99.0f, 99.0f, 99.0f},
        {0.0f, 1.0f, 0.0f},
    };
    std::vector<std::vector<hp2::CpuSkinInfluence>> packed_influences;
    std::string influence_error;
    if (!hp2::BuildCpuSkinInfluences(skinning, packed_influences, &influence_error)
        || packed_influences.size() != 1u || packed_influences[0].size() != 1u) return 7;
    const auto reference_local = hp2::MakeReferenceLocalPose(skinning.bones);
    std::vector<hp2::BoneTransform> reference_model;
    if (!hp2::BuildModelSpacePose(skinning.bones, reference_local, reference_model)) return 8;
    const auto rebound = hp2::CpuSkinPoints(packed_influences, reference_model);
    if (rebound.size() != 1u || !Near(rebound[0], skinning.reference_points[0])) return 9;

    hp2::PackageIndex package;
    package.valid = true;
    package.summary.valid = true;
    package.summary.path = "HPModels.u";
    package.summary.file_version = 79u;
    package.names = {
        {"root", 0u}, {"child", 0u}, {"TestMove", 0u}, {"None", 0u}
    };
    const auto native = SyntheticAnimationNative();
    const hp2::HP2AnimationData animation = hp2::ParseHP2AnimationNative(
        package, "SyntheticAnimation", native
    );
    if (!animation.valid || animation.bones.size() != 2u || animation.moves.size() != 1u
        || animation.sequences.size() != 1u || animation.total_track_count != 2u) return 10;
    if (animation.master_quaternion_count != 4u || animation.master_position_count != 3u
        || animation.master_delta_count != 4u || animation.remaining_bytes != 0u) return 11;
    const auto& first_track = animation.moves[0].tracks[0];
    const auto& second_track = animation.moves[0].tracks[1];
    if (first_track.keys.rotations.size() != 2u || first_track.keys.positions.size() != 1u
        || second_track.keys.rotations.size() != 2u || second_track.keys.positions.size() != 2u) {
        return 12;
    }
    if (!Near(first_track.keys.rotations[1].time, 1.0f)
        || !Near(second_track.keys.positions[1].time, 1.0f)
        || !Near(first_track.keys.positions[0].value, {10.0f, 0.0f, 0.0f})
        || !Near(second_track.keys.positions[0].value, {0.0f, 2.0f, 0.0f})
        || !Near(second_track.keys.positions[1].value, {0.0f, 0.0f, 4.0f})) return 13;
    if (second_track.keys.rotations[0].value.w >= 0.0f
        || !Near(hp2::AnimationMoveDuration(animation.moves[0]), 1.25f)) return 14;

    std::vector<std::uint8_t> truncated = native;
    truncated.pop_back();
    if (hp2::ParseHP2AnimationNative(package, "Truncated", truncated).valid) return 15;

    std::cout << "animation math tests passed\n";
    return 0;
}
