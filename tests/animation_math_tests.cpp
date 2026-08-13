#include "hp2/ue_animation.h"

#include <cmath>
#include <iostream>
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

    std::cout << "animation math tests passed\n";
    return 0;
}
