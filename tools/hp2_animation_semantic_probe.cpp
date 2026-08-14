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

enum class TrackMapping {
    Direct,
    BoneToTrack,
    TrackToBone,
};

const char* TrackMappingName(TrackMapping mapping) {
    switch (mapping) {
        case TrackMapping::Direct: return "direct_track_order";
        case TrackMapping::BoneToTrack: return "bone_to_track_index";
        case TrackMapping::TrackToBone: return "track_to_bone_index";
    }
    return "unknown";
}

struct TrackPair {
    std::size_t bone_index = 0u;
    std::size_t track_index = 0u;
};

std::vector<TrackPair> BuildTrackPairs(
    const hp2::HP2AnimationMove& move,
    std::size_t bone_count,
    TrackMapping mapping
) {
    std::vector<TrackPair> result;
    if (mapping == TrackMapping::Direct || move.bone_indices.empty()) {
        const std::size_t count = std::min(bone_count, move.tracks.size());
        result.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            result.push_back({index, index});
        }
        return result;
    }
    if (mapping == TrackMapping::BoneToTrack) {
        const std::size_t count = std::min(bone_count, move.bone_indices.size());
        result.reserve(count);
        for (std::size_t bone_index = 0; bone_index < count; ++bone_index) {
            const std::int32_t track_index = move.bone_indices[bone_index];
            if (track_index >= 0 && static_cast<std::size_t>(track_index) < move.tracks.size()) {
                result.push_back({bone_index, static_cast<std::size_t>(track_index)});
            }
        }
        return result;
    }
    const std::size_t count = std::min(move.tracks.size(), move.bone_indices.size());
    result.reserve(count);
    for (std::size_t track_index = 0; track_index < count; ++track_index) {
        const std::int32_t bone_index = move.bone_indices[track_index];
        if (bone_index >= 0 && static_cast<std::size_t>(bone_index) < bone_count) {
            result.push_back({static_cast<std::size_t>(bone_index), track_index});
        }
    }
    return result;
}

float SignedComponent(float value, int sign_mask, int bit) {
    return (sign_mask & (1 << bit)) != 0 ? -value : value;
}

hp2::Quaternion CandidateQuaternion(
    const hp2::Quaternion& decoded,
    bool linear_components,
    int sign_mask,
    int w_sign
) {
    constexpr float kHalfPi = 1.57079632679f;
    auto component = [&](float value, int bit) {
        float decoded_component = value;
        if (linear_components) {
            decoded_component = std::asin(std::max(-1.0f, std::min(1.0f, value))) / kHalfPi;
        }
        return SignedComponent(decoded_component, sign_mask, bit);
    };
    const float x = component(decoded.x, 0);
    const float y = component(decoded.y, 1);
    const float z = component(decoded.z, 2);
    const float w_squared = std::max(0.0f, 1.0f - x * x - y * y - z * z);
    const float w = static_cast<float>(w_sign) * std::sqrt(w_squared);
    return hp2::NormalizeQuaternion({x, y, z, w});
}

struct QuaternionAlignment {
    TrackMapping mapping = TrackMapping::Direct;
    bool linear_components = false;
    int sign_mask = 0;
    int w_sign = 1;
    std::size_t samples = 0u;
    double mean_error = std::numeric_limits<double>::infinity();
};

QuaternionAlignment MeasureQuaternionAlignment(
    const hp2::HP2AnimationData& animation,
    const std::vector<hp2::BoneTransform>& reference_local
) {
    QuaternionAlignment best;
    for (const TrackMapping mapping : {
             TrackMapping::Direct, TrackMapping::BoneToTrack, TrackMapping::TrackToBone
         }) {
        for (const bool linear_components : {false, true}) {
            for (int sign_mask = 0; sign_mask < 8; ++sign_mask) {
                for (const int w_sign : {-1, 1}) {
                    double total_error = 0.0;
                    std::size_t samples = 0u;
                    for (const hp2::HP2AnimationMove& move : animation.moves) {
                        for (const TrackPair& pair : BuildTrackPairs(
                                 move, reference_local.size(), mapping
                             )) {
                            const auto& keys = move.tracks[pair.track_index].keys.rotations;
                            if (keys.empty() || pair.bone_index == 0u) continue;
                            const hp2::Quaternion candidate = CandidateQuaternion(
                                keys.front().value, linear_components, sign_mask, w_sign
                            );
                            const hp2::Quaternion reference = hp2::NormalizeQuaternion(
                                reference_local[pair.bone_index].rotation
                            );
                            const double dot = std::fabs(
                                static_cast<double>(candidate.x) * reference.x
                                + static_cast<double>(candidate.y) * reference.y
                                + static_cast<double>(candidate.z) * reference.z
                                + static_cast<double>(candidate.w) * reference.w
                            );
                            total_error += 1.0 - std::min(1.0, dot);
                            ++samples;
                        }
                    }
                    const double mean_error = samples > 0u
                        ? total_error / static_cast<double>(samples)
                        : std::numeric_limits<double>::infinity();
                    if (mean_error < best.mean_error) {
                        best = {mapping, linear_components, sign_mask, w_sign,
                                samples, mean_error};
                    }
                }
            }
        }
    }
    return best;
}

struct PositionAlignment {
    TrackMapping mapping = TrackMapping::Direct;
    int sign_mask = 0;
    std::size_t samples = 0u;
    double relative_rms = std::numeric_limits<double>::infinity();
};

PositionAlignment MeasurePositionAlignment(
    const hp2::HP2AnimationData& animation,
    const std::vector<hp2::BoneTransform>& reference_local
) {
    PositionAlignment best;
    for (const TrackMapping mapping : {
             TrackMapping::Direct, TrackMapping::BoneToTrack, TrackMapping::TrackToBone
         }) {
        for (int sign_mask = 0; sign_mask < 8; ++sign_mask) {
            double squared_error = 0.0;
            double squared_reference = 0.0;
            std::size_t samples = 0u;
            for (const hp2::HP2AnimationMove& move : animation.moves) {
                for (const TrackPair& pair : BuildTrackPairs(
                         move, reference_local.size(), mapping
                     )) {
                    const auto& keys = move.tracks[pair.track_index].keys.positions;
                    if (keys.empty() || pair.bone_index == 0u) continue;
                    const hp2::Vec3 candidate{
                        SignedComponent(keys.front().value.x, sign_mask, 0),
                        SignedComponent(keys.front().value.y, sign_mask, 1),
                        SignedComponent(keys.front().value.z, sign_mask, 2),
                    };
                    const hp2::Vec3& reference = reference_local[pair.bone_index].translation;
                    const double x = static_cast<double>(candidate.x) - reference.x;
                    const double y = static_cast<double>(candidate.y) - reference.y;
                    const double z = static_cast<double>(candidate.z) - reference.z;
                    squared_error += x * x + y * y + z * z;
                    squared_reference += static_cast<double>(reference.x) * reference.x
                        + static_cast<double>(reference.y) * reference.y
                        + static_cast<double>(reference.z) * reference.z;
                    ++samples;
                }
            }
            const double relative_rms = samples > 0u && squared_reference > 0.0
                ? std::sqrt(squared_error / squared_reference)
                : std::numeric_limits<double>::infinity();
            if (relative_rms < best.relative_rms) {
                best = {mapping, sign_mask, samples, relative_rms};
            }
        }
    }
    return best;
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

    const std::vector<hp2::BoneTransform> reference_local = hp2::MakeReferenceLocalPose(
        skinning.bones
    );
    const QuaternionAlignment quaternion_alignment = MeasureQuaternionAlignment(
        animation, reference_local
    );
    const PositionAlignment position_alignment = MeasurePositionAlignment(
        animation, reference_local
    );
    std::size_t bone_map_entries = 0u;
    std::size_t bone_map_identity_entries = 0u;
    std::size_t bone_map_negative_entries = 0u;
    std::size_t bone_map_invalid_entries = 0u;
    for (const hp2::HP2AnimationMove& move : animation.moves) {
        for (std::size_t index = 0; index < move.bone_indices.size(); ++index) {
            const std::int32_t value = move.bone_indices[index];
            ++bone_map_entries;
            bone_map_identity_entries += value == static_cast<std::int32_t>(index) ? 1u : 0u;
            bone_map_negative_entries += value < 0 ? 1u : 0u;
            bone_map_invalid_entries += value >= static_cast<std::int32_t>(move.tracks.size())
                ? 1u : 0u;
        }
    }
    const bool bone_map_identity = bone_map_entries > 0u
        && bone_map_identity_entries == bone_map_entries
        && bone_map_negative_entries == 0u && bone_map_invalid_entries == 0u;
    const bool convention_confirmed = bone_map_identity
        && quaternion_alignment.mapping == TrackMapping::Direct
        && !quaternion_alignment.linear_components
        && quaternion_alignment.sign_mask == 0 && quaternion_alignment.w_sign == -1
        && quaternion_alignment.samples > 0u && quaternion_alignment.mean_error <= 0.02
        && position_alignment.mapping == TrackMapping::Direct
        && position_alignment.sign_mask == 0 && position_alignment.samples > 0u
        && position_alignment.relative_rms <= 0.25;

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
            float deformation = 0.0f;
            if (!hp2::IsPlausibleDiagnosticPose(
                    skinning.reference_points, points, &deformation
                )) {
                continue;
            }
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
        && bind_valid && convention_confirmed && stable_moves > 0u
        && maximum_deformation > 0.002f;
    const std::string error = !animation.valid ? animation.error
        : !influences_valid ? influence_error
        : !skeleton_matches ? "mesh and animation skeletons differ"
        : !bind_valid ? "reference-pose skinning mismatch"
        : !convention_confirmed ? "animation key convention mismatch"
        : stable_moves == 0u ? "no stable animation move" : std::string{};

    std::cout << "{\n"
              << "  \"schema\":\"hp2-animation-runtime-v2\",\n"
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
              << "  \"bone_map_entries\":" << bone_map_entries << ",\n"
              << "  \"bone_map_identity_entries\":" << bone_map_identity_entries << ",\n"
              << "  \"bone_map_negative_entries\":" << bone_map_negative_entries << ",\n"
              << "  \"bone_map_invalid_entries\":" << bone_map_invalid_entries << ",\n"
              << "  \"convention_confirmed\":"
              << (convention_confirmed ? "true" : "false") << ",\n"
              << "  \"best_quaternion_mapping\":\""
              << TrackMappingName(quaternion_alignment.mapping) << "\",\n"
              << "  \"best_quaternion_components\":\""
              << (quaternion_alignment.linear_components ? "linear" : "sine") << "\",\n"
              << "  \"best_quaternion_sign_mask\":" << quaternion_alignment.sign_mask << ",\n"
              << "  \"best_quaternion_w_sign\":" << quaternion_alignment.w_sign << ",\n"
              << "  \"best_quaternion_samples\":" << quaternion_alignment.samples << ",\n"
              << "  \"best_quaternion_mean_error\":"
              << (std::isfinite(quaternion_alignment.mean_error)
                      ? quaternion_alignment.mean_error : 0.0) << ",\n"
              << "  \"best_position_mapping\":\""
              << TrackMappingName(position_alignment.mapping) << "\",\n"
              << "  \"best_position_sign_mask\":" << position_alignment.sign_mask << ",\n"
              << "  \"best_position_samples\":" << position_alignment.samples << ",\n"
              << "  \"best_position_relative_rms\":"
              << (std::isfinite(position_alignment.relative_rms)
                      ? position_alignment.relative_rms : 0.0) << ",\n"
              << "  \"stable_moves\":" << stable_moves << ",\n"
              << "  \"max_deformation\":" << maximum_deformation << ",\n"
              << "  \"first_stable_sequence\":\""
              << JsonEscape(first_stable_sequence) << "\"\n"
              << "}\n";
    return runtime_ok ? 0 : 5;
}
