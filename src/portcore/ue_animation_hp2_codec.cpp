#include "hp2/ue_animation.h"

#include <algorithm>
#include <cmath>

namespace hp2 {
namespace {

constexpr float kHp2AngleScale = 1.57079633f / 32767.0f;
constexpr float kHp2VectorScale = 1.0f / 32767.0f;

}  // namespace

Quaternion DecodeHP2PackedQuaternion(
    std::int16_t raw_x,
    std::int16_t raw_y,
    std::int16_t raw_z,
    bool root_track
) {
    const float x = std::sin(static_cast<float>(raw_x) * kHp2AngleScale);
    const float y = -std::sin(static_cast<float>(raw_y) * kHp2AngleScale);
    const float z = std::sin(static_cast<float>(raw_z) * kHp2AngleScale);
    float w = std::sqrt(std::max(0.0f, 1.0f - x * x - y * y - z * z));
    if (!root_track) w = -w;
    return NormalizeQuaternion({x, y, z, w});
}

Vec3 DecodeHP2PackedPosition(
    std::int16_t raw_x,
    std::int16_t raw_y,
    std::int16_t raw_z,
    float position_scale
) {
    const float factor = position_scale * kHp2VectorScale;
    return {
        static_cast<float>(raw_x) * factor,
        -static_cast<float>(raw_y) * factor,
        static_cast<float>(raw_z) * factor,
    };
}

std::vector<float> DecodeHP2KeyDeltas(const std::vector<std::uint8_t>& deltas) {
    std::vector<float> times;
    times.reserve(deltas.size());
    std::uint32_t current_time = 0;
    for (const std::uint8_t delta : deltas) {
        current_time += delta;
        times.push_back(static_cast<float>(current_time));
    }
    return times;
}

}  // namespace hp2
