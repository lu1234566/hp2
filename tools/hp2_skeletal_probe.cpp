#include "hp2/ue_skeletal.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>

namespace {

std::string JsonEscape(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (const char character : value) {
        switch (character) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (static_cast<unsigned char>(character) >= 0x20u) {
                    result += character;
                }
                break;
        }
    }
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: hp2_skeletal_probe <game-root> <package-name> <object-name>\n";
        return 64;
    }

    const hp2::SkeletalMeshSkinningData data = hp2::LoadNamedSkeletalMeshSkinning(
        std::filesystem::path(argv[1]), argv[2], argv[3]
    );

    float min_weight = std::numeric_limits<float>::infinity();
    float max_weight = -std::numeric_limits<float>::infinity();
    for (const auto& word : data.weight_words) {
        if (!word.finite) {
            continue;
        }
        min_weight = std::min(min_weight, word.as_float);
        max_weight = std::max(max_weight, word.as_float);
    }
    const bool have_finite_weight = data.finite_weight_words > 0;

    std::cout << "{\n"
              << "  \"schema\": \"hp2-skeletal-probe-v1\",\n"
              << "  \"valid\": " << (data.valid ? "true" : "false") << ",\n"
              << "  \"package_name\": \"" << JsonEscape(data.package_name) << "\",\n"
              << "  \"object_name\": \"" << JsonEscape(data.object_name) << "\",\n"
              << "  \"version\": " << data.file_version << ",\n"
              << "  \"frame_vertices\": " << data.frame_vertices << ",\n"
              << "  \"animation_frames\": " << data.animation_frames << ",\n"
              << "  \"sequence_count\": " << data.sequences.size() << ",\n"
              << "  \"reference_points\": " << data.reference_point_count << ",\n"
              << "  \"bones\": " << data.bones.size() << ",\n"
              << "  \"root_like_bones\": " << data.root_like_bones << ",\n"
              << "  \"invalid_parent_bones\": " << data.invalid_parent_bones << ",\n"
              << "  \"weight_index_records\": " << data.weight_indices.size() << ",\n"
              << "  \"weight_index_first_max\": " << data.weight_index_first_max << ",\n"
              << "  \"weight_index_second_max\": " << data.weight_index_second_max << ",\n"
              << "  \"weight_words\": " << data.weight_words.size() << ",\n"
              << "  \"finite_weight_words\": " << data.finite_weight_words << ",\n"
              << "  \"unit_interval_weight_words\": " << data.unit_interval_weight_words << ",\n"
              << "  \"nonfinite_weight_words\": " << data.nonfinite_weight_words << ",\n"
              << "  \"finite_weight_min\": ";
    if (have_finite_weight) {
        std::cout << min_weight;
    } else {
        std::cout << "null";
    }
    std::cout << ",\n  \"finite_weight_max\": ";
    if (have_finite_weight) {
        std::cout << max_weight;
    } else {
        std::cout << "null";
    }
    std::cout << ",\n"
              << "  \"local_points\": " << data.local_points.size() << ",\n"
              << "  \"remaining_bytes\": " << data.remaining_bytes << ",\n"
              << "  \"sequences\": [\n";
    for (std::size_t index = 0; index < data.sequences.size(); ++index) {
        const auto& sequence = data.sequences[index];
        std::cout << "    {\"name\": \"" << JsonEscape(sequence.name)
                  << "\", \"group\": \"" << JsonEscape(sequence.group)
                  << "\", \"start_frame\": " << sequence.start_frame
                  << ", \"frame_count\": " << sequence.frame_count
                  << ", \"notify_count\": " << sequence.notify_count
                  << ", \"rate\": " << sequence.rate << "}"
                  << (index + 1 == data.sequences.size() ? "" : ",") << '\n';
    }
    std::cout << "  ]";
    if (!data.error.empty()) {
        std::cout << ",\n  \"error\": \"" << JsonEscape(data.error) << "\"\n";
    } else {
        std::cout << "\n";
    }
    std::cout << "}\n";
    return data.valid ? 0 : 2;
}
