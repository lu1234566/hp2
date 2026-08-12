#include "hp2/ue_skeletal.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_set>

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

struct IndexLaneStats {
    std::uint16_t low_min = std::numeric_limits<std::uint16_t>::max();
    std::uint16_t low_max = 0;
    std::uint16_t high_min = std::numeric_limits<std::uint16_t>::max();
    std::uint16_t high_max = 0;
    std::uint64_t low_sum = 0;
    std::uint64_t high_sum = 0;
    std::size_t second_nonzero = 0;
    std::size_t low_within_influences = 0;
    std::size_t high_within_influences = 0;
    std::size_t low_plus_high_within_influences = 0;
    std::uint32_t low_plus_high_max = 0;
    bool low_monotonic = true;
    bool high_monotonic = true;
    std::size_t low_unique = 0;
    std::size_t high_unique = 0;
};

IndexLaneStats MeasureIndexLanes(const hp2::SkeletalMeshSkinningData& data) {
    IndexLaneStats stats;
    std::unordered_set<std::uint16_t> low_values;
    std::unordered_set<std::uint16_t> high_values;
    std::uint16_t previous_low = 0;
    std::uint16_t previous_high = 0;
    bool first = true;
    const std::size_t influence_count = data.weight_words.size();

    for (const auto& record : data.weight_indices) {
        const auto low = static_cast<std::uint16_t>(record.first & 0xffffu);
        const auto high = static_cast<std::uint16_t>((record.first >> 16u) & 0xffffu);
        stats.low_min = std::min(stats.low_min, low);
        stats.low_max = std::max(stats.low_max, low);
        stats.high_min = std::min(stats.high_min, high);
        stats.high_max = std::max(stats.high_max, high);
        stats.low_sum += low;
        stats.high_sum += high;
        stats.second_nonzero += record.second != 0u ? 1u : 0u;
        stats.low_within_influences += low <= influence_count ? 1u : 0u;
        stats.high_within_influences += high <= influence_count ? 1u : 0u;
        const std::uint32_t end_candidate = static_cast<std::uint32_t>(low)
            + static_cast<std::uint32_t>(high);
        stats.low_plus_high_max = std::max(stats.low_plus_high_max, end_candidate);
        stats.low_plus_high_within_influences += end_candidate <= influence_count ? 1u : 0u;
        if (!first) {
            stats.low_monotonic = stats.low_monotonic && low >= previous_low;
            stats.high_monotonic = stats.high_monotonic && high >= previous_high;
        }
        previous_low = low;
        previous_high = high;
        first = false;
        low_values.insert(low);
        high_values.insert(high);
    }
    if (data.weight_indices.empty()) {
        stats.low_min = 0;
        stats.high_min = 0;
    }
    stats.low_unique = low_values.size();
    stats.high_unique = high_values.size();
    return stats;
}

struct WeightLaneStats {
    std::uint16_t low16_min = std::numeric_limits<std::uint16_t>::max();
    std::uint16_t low16_max = 0;
    std::uint16_t high16_min = std::numeric_limits<std::uint16_t>::max();
    std::uint16_t high16_max = 0;
    std::array<std::uint8_t, 4> byte_min{255u, 255u, 255u, 255u};
    std::array<std::uint8_t, 4> byte_max{0u, 0u, 0u, 0u};
    std::array<std::size_t, 4> byte_within_bones{};
    std::array<std::size_t, 4> byte_unique{};
    std::size_t low16_within_points = 0;
    std::size_t high16_within_points = 0;
    std::size_t low16_within_bones = 0;
    std::size_t high16_within_bones = 0;
    std::size_t low16_unique = 0;
    std::size_t high16_unique = 0;
    std::size_t raw_zero = 0;
};

WeightLaneStats MeasureWeightLanes(const hp2::SkeletalMeshSkinningData& data) {
    WeightLaneStats stats;
    std::unordered_set<std::uint16_t> low16_values;
    std::unordered_set<std::uint16_t> high16_values;
    std::array<std::unordered_set<std::uint8_t>, 4> byte_values;

    for (const auto& word : data.weight_words) {
        const std::uint16_t low16 = static_cast<std::uint16_t>(word.raw & 0xffffu);
        const std::uint16_t high16 = static_cast<std::uint16_t>((word.raw >> 16u) & 0xffffu);
        stats.low16_min = std::min(stats.low16_min, low16);
        stats.low16_max = std::max(stats.low16_max, low16);
        stats.high16_min = std::min(stats.high16_min, high16);
        stats.high16_max = std::max(stats.high16_max, high16);
        stats.low16_within_points += low16 < data.reference_point_count ? 1u : 0u;
        stats.high16_within_points += high16 < data.reference_point_count ? 1u : 0u;
        stats.low16_within_bones += low16 < data.bones.size() ? 1u : 0u;
        stats.high16_within_bones += high16 < data.bones.size() ? 1u : 0u;
        stats.raw_zero += word.raw == 0u ? 1u : 0u;
        low16_values.insert(low16);
        high16_values.insert(high16);
        for (std::size_t lane = 0; lane < 4; ++lane) {
            const auto byte = static_cast<std::uint8_t>((word.raw >> (lane * 8u)) & 0xffu);
            stats.byte_min[lane] = std::min(stats.byte_min[lane], byte);
            stats.byte_max[lane] = std::max(stats.byte_max[lane], byte);
            stats.byte_within_bones[lane] += byte < data.bones.size() ? 1u : 0u;
            byte_values[lane].insert(byte);
        }
    }
    if (data.weight_words.empty()) {
        stats.low16_min = 0;
        stats.high16_min = 0;
        stats.byte_min = {0u, 0u, 0u, 0u};
    }
    stats.low16_unique = low16_values.size();
    stats.high16_unique = high16_values.size();
    for (std::size_t lane = 0; lane < 4; ++lane) {
        stats.byte_unique[lane] = byte_values[lane].size();
    }
    return stats;
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
    const IndexLaneStats index_lanes = MeasureIndexLanes(data);
    const WeightLaneStats weight_lanes = MeasureWeightLanes(data);

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
              << "  \"schema\": \"hp2-skeletal-probe-v2\",\n"
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
              << "  \"weight_index_second_nonzero\": " << index_lanes.second_nonzero << ",\n"
              << "  \"weight_index_low16_min\": " << index_lanes.low_min << ",\n"
              << "  \"weight_index_low16_max\": " << index_lanes.low_max << ",\n"
              << "  \"weight_index_low16_sum\": " << index_lanes.low_sum << ",\n"
              << "  \"weight_index_low16_unique\": " << index_lanes.low_unique << ",\n"
              << "  \"weight_index_low16_monotonic\": " << (index_lanes.low_monotonic ? "true" : "false") << ",\n"
              << "  \"weight_index_high16_min\": " << index_lanes.high_min << ",\n"
              << "  \"weight_index_high16_max\": " << index_lanes.high_max << ",\n"
              << "  \"weight_index_high16_sum\": " << index_lanes.high_sum << ",\n"
              << "  \"weight_index_high16_unique\": " << index_lanes.high_unique << ",\n"
              << "  \"weight_index_high16_monotonic\": " << (index_lanes.high_monotonic ? "true" : "false") << ",\n"
              << "  \"weight_index_low16_within_influences\": " << index_lanes.low_within_influences << ",\n"
              << "  \"weight_index_high16_within_influences\": " << index_lanes.high_within_influences << ",\n"
              << "  \"weight_index_low_plus_high_max\": " << index_lanes.low_plus_high_max << ",\n"
              << "  \"weight_index_low_plus_high_within_influences\": " << index_lanes.low_plus_high_within_influences << ",\n"
              << "  \"weight_words\": " << data.weight_words.size() << ",\n"
              << "  \"weight_word_raw_zero\": " << weight_lanes.raw_zero << ",\n"
              << "  \"weight_word_low16_min\": " << weight_lanes.low16_min << ",\n"
              << "  \"weight_word_low16_max\": " << weight_lanes.low16_max << ",\n"
              << "  \"weight_word_low16_unique\": " << weight_lanes.low16_unique << ",\n"
              << "  \"weight_word_low16_within_points\": " << weight_lanes.low16_within_points << ",\n"
              << "  \"weight_word_low16_within_bones\": " << weight_lanes.low16_within_bones << ",\n"
              << "  \"weight_word_high16_min\": " << weight_lanes.high16_min << ",\n"
              << "  \"weight_word_high16_max\": " << weight_lanes.high16_max << ",\n"
              << "  \"weight_word_high16_unique\": " << weight_lanes.high16_unique << ",\n"
              << "  \"weight_word_high16_within_points\": " << weight_lanes.high16_within_points << ",\n"
              << "  \"weight_word_high16_within_bones\": " << weight_lanes.high16_within_bones << ",\n"
              << "  \"weight_word_byte_min\": ["
              << static_cast<unsigned>(weight_lanes.byte_min[0]) << ", "
              << static_cast<unsigned>(weight_lanes.byte_min[1]) << ", "
              << static_cast<unsigned>(weight_lanes.byte_min[2]) << ", "
              << static_cast<unsigned>(weight_lanes.byte_min[3]) << "],\n"
              << "  \"weight_word_byte_max\": ["
              << static_cast<unsigned>(weight_lanes.byte_max[0]) << ", "
              << static_cast<unsigned>(weight_lanes.byte_max[1]) << ", "
              << static_cast<unsigned>(weight_lanes.byte_max[2]) << ", "
              << static_cast<unsigned>(weight_lanes.byte_max[3]) << "],\n"
              << "  \"weight_word_byte_unique\": ["
              << weight_lanes.byte_unique[0] << ", " << weight_lanes.byte_unique[1] << ", "
              << weight_lanes.byte_unique[2] << ", " << weight_lanes.byte_unique[3] << "],\n"
              << "  \"weight_word_byte_within_bones\": ["
              << weight_lanes.byte_within_bones[0] << ", " << weight_lanes.byte_within_bones[1] << ", "
              << weight_lanes.byte_within_bones[2] << ", " << weight_lanes.byte_within_bones[3] << "],\n"
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
