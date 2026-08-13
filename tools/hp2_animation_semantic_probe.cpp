#include "hp2/ue_animation.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string JsonEscape(const std::string& value) {
    std::string result;
    for (const char character : value) {
        switch (character) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (static_cast<unsigned char>(character) >= 0x20u) result += character;
                break;
        }
    }
    return result;
}

struct Bounds {
    bool valid = false;
    hp2::Vec3 minimum;
    hp2::Vec3 maximum;
    hp2::Vec3 center;
    float extent = 0.0f;
};

Bounds Measure(const std::vector<hp2::Vec3>& points) {
    Bounds result;
    for (const hp2::Vec3& point : points) {
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
    result.extent = std::max({
        result.maximum.x - result.minimum.x,
        result.maximum.y - result.minimum.y,
        result.maximum.z - result.minimum.z,
    });
    result.valid = std::isfinite(result.extent) && result.extent > 0.0f;
    return result;
}

float DeformationScore(
    const std::vector<hp2::Vec3>& reference,
    const std::vector<hp2::Vec3>& candidate
) {
    if (reference.size() != candidate.size() || reference.empty()) return -1.0f;
    const Bounds a = Measure(reference);
    const Bounds b = Measure(candidate);
    if (!a.valid || !b.valid) return -1.0f;
    const float ratio = b.extent / a.extent;
    if (!std::isfinite(ratio) || ratio < 0.25f || ratio > 4.0f) return -1.0f;
    const hp2::Vec3 center_delta = {
        b.center.x - a.center.x,
        b.center.y - a.center.y,
        b.center.z - a.center.z,
    };
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
        std::sqrt(squared / static_cast<double>(reference.size())) / a.extent
    );
    return std::isfinite(score) && score <= 2.0f ? score : -1.0f;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: hp2_animation_semantic_probe <game-root> <package> <animation>\n";
        return 64;
    }

    const hp2::HP2AnimationData animation = hp2::LoadNamedHP2Animation(
        argv[1], argv[2], argv[3]
    );
    const hp2::SkeletalMeshSkinningData skinning = hp2::LoadNamedSkeletalMeshSkinning(
        argv[1], argv[2], "skhp2_genmale1Mesh"
    );
    std::vector<std::vector<hp2::CpuSkinInfluence>> influences;
    std::string influence_error;
    const bool influences_valid = hp2::BuildCpuSkinInfluences(
        skinning, influences, &influence_error
    );

    bool skeleton_matches = animation.valid && skinning.valid
        && animation.bones.size() == skinning.bones.size();
    if (skeleton_matches) {
        for (std::size_t index = 0; index < animation.bones.size(); ++index) {
            if (Lowercase(animation.bones[index].name) != Lowercase(skinning.bones[index].name)
                || animation.bones[index].parent_index != skinning.bones[index].parent_index) {
                skeleton_matches = false;
                break;
            }
        }
    }

    double bind_squared = 0.0;
    float bind_max = std::numeric_limits<float>::infinity();
    bool bind_valid = false;
    if (influences_valid) {
        const auto local = hp2::MakeReferenceLocalPose(skinning.bones);
        std::vector<hp2::BoneTransform> model;
        std::string pose_error;
        if (hp2::BuildModelSpacePose(skinning.bones, local, model, &pose_error)) {
            const auto points = hp2::CpuSkinPoints(influences, model);
            if (points.size() == skinning.reference_points.size() && !points.empty()) {
                bind_max = 0.0f;
                for (std::size_t index = 0; index < points.size(); ++index) {
                    const float x = points[index].x - skinning.reference_points[index].x;
                    const float y = points[index].y - skinning.reference_points[index].y;
                    const float z = points[index].z - skinning.reference_points[index].z;
                    const float distance = std::sqrt(x * x + y * y + z * z);
                    bind_max = std::max(bind_max, distance);
                    bind_squared += static_cast<double>(distance) * distance;
                }
                bind_valid = std::isfinite(bind_max) && bind_max <= 0.01f;
            }
        }
    }
    const double bind_rms = bind_valid
        ? std::sqrt(bind_squared / static_cast<double>(skinning.reference_points.size())) : 0.0;

    std::size_t stable_moves = 0u;
    float maximum_deformation = 0.0f;
    std::string first_stable_sequence;
    if (animation.valid && influences_valid && skeleton_matches) {
        for (std::size_t move_index = 0; move_index < animation.moves.size(); ++move_index) {
            const float duration = hp2::AnimationMoveDuration(animation.moves[move_index]);
            if (duration <= 0.0f || !std::isfinite(duration)) continue;
            std::vector<hp2::Vec3> points;
            std::string sample_error;
            if (!hp2::SampleAnimationMovePoints(
                    skinning, animation, move_index, duration * 0.5f, influences,
                    points, &sample_error
                )) {
                continue;
            }
            const float deformation = DeformationScore(skinning.reference_points, points);
            if (deformation < 0.0f) continue;
            ++stable_moves;
            maximum_deformation = std::max(maximum_deformation, deformation);
            if (first_stable_sequence.empty() && move_index < animation.sequences.size()) {
                first_stable_sequence = animation.sequences[move_index].name;
            }
        }
    }

    std::size_t minimum_tracks = animation.moves.empty()
        ? 0u : std::numeric_limits<std::size_t>::max();
    std::size_t maximum_tracks = 0u;
    for (const hp2::HP2AnimationMove& move : animation.moves) {
        minimum_tracks = std::min(minimum_tracks, move.tracks.size());
        maximum_tracks = std::max(maximum_tracks, move.tracks.size());
    }
    const bool runtime_ok = animation.valid && influences_valid && skeleton_matches
        && bind_valid && stable_moves > 0u && maximum_deformation > 0.002f;
    const std::string error = !animation.valid ? animation.error
        : !influences_valid ? influence_error
        : !skeleton_matches ? "mesh and animation skeletons differ"
        : !bind_valid ? "reference-pose skinning mismatch"
        : stable_moves == 0u ? "no stable animation move" : std::string{};

    std::cout << "{\n"
              << "  \"schema\":\"hp2-animation-runtime-v1\",\n"
              << "  \"valid\":" << (runtime_ok ? "true" : "false") << ",\n"
              << "  \"error\":\"" << JsonEscape(error) << "\",\n"
              << "  \"file_version\":" << animation.file_version << ",\n"
              << "  \"package\":\"" << JsonEscape(animation.package_name) << "\",\n"
              << "  \"object\":\"" << JsonEscape(animation.object_name) << "\",\n"
              << "  \"bones\":" << animation.bones.size() << ",\n"
              << "  \"moves\":" << animation.moves.size() << ",\n"
              << "  \"sequences\":" << animation.sequences.size() << ",\n"
              << "  \"tracks\":" << animation.total_track_count << ",\n"
              << "  \"track_min\":" << minimum_tracks << ",\n"
              << "  \"track_max\":" << maximum_tracks << ",\n"
              << "  \"master_quaternions\":" << animation.master_quaternion_count << ",\n"
              << "  \"master_positions\":" << animation.master_position_count << ",\n"
              << "  \"master_deltas\":" << animation.master_delta_count << ",\n"
              << "  \"remaining_bytes\":" << animation.remaining_bytes << ",\n"
              << "  \"mesh_points\":" << skinning.reference_points.size() << ",\n"
              << "  \"mesh_bones\":" << skinning.bones.size() << ",\n"
              << "  \"animation_reference\":" << skinning.animation_reference << ",\n"
              << "  \"animation_reference_object\":\""
              << JsonEscape(skinning.animation_object_name) << "\",\n"
              << "  \"influence_slots\":" << skinning.weight_words.size() << ",\n"
              << "  \"skeleton_matches\":" << (skeleton_matches ? "true" : "false") << ",\n"
              << "  \"bind_valid\":" << (bind_valid ? "true" : "false") << ",\n"
              << "  \"bind_rms\":" << bind_rms << ",\n"
              << "  \"bind_max\":" << (std::isfinite(bind_max) ? bind_max : 0.0f) << ",\n"
              << "  \"stable_moves\":" << stable_moves << ",\n"
              << "  \"max_deformation\":" << maximum_deformation << ",\n"
              << "  \"first_stable_sequence\":\""
              << JsonEscape(first_stable_sequence) << "\"\n"
              << "}\n";
    return runtime_ok ? 0 : 5;
}
