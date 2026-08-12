#include "hp2/ue_lightmap.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <vector>

namespace hp2 {
namespace {

constexpr std::size_t kMaxAtlasBytes = 128u * 1024u * 1024u;

struct Patch {
    std::size_t light_map_index = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
};

std::int32_t NextPowerOfTwo(std::int32_t value) {
    if (value <= 1) {
        return 1;
    }
    std::uint32_t result = 1;
    while (result < static_cast<std::uint32_t>(value) && result < (1u << 30u)) {
        result <<= 1u;
    }
    if (result < static_cast<std::uint32_t>(value)
        || result > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
        throw std::runtime_error("light-map atlas dimension overflow");
    }
    return static_cast<std::int32_t>(result);
}

std::size_t CountLightActors(const ModelGeometry& geometry, const LightMapIndex& light_map) {
    if (light_map.light_actors < 0) {
        return 0;
    }
    const std::size_t first = static_cast<std::size_t>(light_map.light_actors);
    if (first >= geometry.light_actor_references.size()) {
        throw std::runtime_error("light-map actor offset is outside the model light list");
    }
    std::size_t count = 0;
    for (std::size_t index = first; index < geometry.light_actor_references.size(); ++index) {
        if (geometry.light_actor_references[index] == 0) {
            return count;
        }
        ++count;
    }
    throw std::runtime_error("light-map actor list has no null terminator");
}

std::vector<float> DecodeBlurredMask(
    const ModelGeometry& geometry,
    const LightMapIndex& light_map,
    std::size_t light_index
) {
    const std::size_t width = static_cast<std::size_t>(light_map.u_clamp);
    const std::size_t height = static_cast<std::size_t>(light_map.v_clamp);
    const std::size_t pitch = (width + 7u) / 8u;
    const std::size_t mask_bytes = pitch * height;
    const std::size_t offset = static_cast<std::size_t>(light_map.data_offset)
        + light_index * mask_bytes;
    if (offset > geometry.light_bits.size()
        || mask_bytes > geometry.light_bits.size() - offset) {
        throw std::runtime_error("light-map shadow mask exceeds the light-bit array");
    }

    std::vector<float> unpacked(width * height, 0.0f);
    for (std::size_t y = 0; y < height; ++y) {
        const std::uint8_t* row = geometry.light_bits.data() + offset + y * pitch;
        for (std::size_t x = 0; x < width; ++x) {
            unpacked[y * width + x] = (row[x >> 3u] & (1u << (x & 7u))) != 0u ? 1.0f : 0.0f;
        }
    }

    static constexpr std::array<float, 9> kWeights = {
        0.0625f, 0.125f, 0.0625f,
        0.125f, 0.25f, 0.125f,
        0.0625f, 0.125f, 0.0625f
    };
    std::vector<float> blurred(width * height, 0.0f);
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            float value = 0.0f;
            for (int dy = -1; dy <= 1; ++dy) {
                const std::size_t sample_y = static_cast<std::size_t>(std::clamp(
                    static_cast<int>(y) + dy, 0, static_cast<int>(height) - 1
                ));
                for (int dx = -1; dx <= 1; ++dx) {
                    const std::size_t sample_x = static_cast<std::size_t>(std::clamp(
                        static_cast<int>(x) + dx, 0, static_cast<int>(width) - 1
                    ));
                    value += unpacked[sample_y * width + sample_x]
                        * kWeights[static_cast<std::size_t>((dy + 1) * 3 + dx + 1)];
                }
            }
            blurred[y * width + x] = value;
        }
    }
    return blurred;
}

bool PackPatches(
    const std::vector<Patch>& patches,
    std::int32_t atlas_width,
    std::int32_t maximum_dimension,
    std::vector<LightMapAtlasRegion>& regions,
    std::int32_t& used_height
) {
    std::int32_t x = 1;
    std::int32_t y = 1;
    std::int32_t row_height = 0;
    for (const Patch& patch : patches) {
        if (patch.width + 2 > atlas_width || patch.height + 2 > maximum_dimension) {
            return false;
        }
        if (x + patch.width + 1 > atlas_width) {
            x = 1;
            y += row_height + 2;
            row_height = 0;
        }
        if (y + patch.height + 1 > maximum_dimension) {
            return false;
        }
        regions[patch.light_map_index] = {x, y, patch.width, patch.height, true};
        x += patch.width + 2;
        row_height = std::max(row_height, patch.height);
    }
    used_height = y + row_height + 1;
    return true;
}

void WritePixel(
    LightMapAtlas& atlas,
    std::int32_t x,
    std::int32_t y,
    std::uint8_t value
) {
    const std::size_t offset = (
        static_cast<std::size_t>(y) * static_cast<std::size_t>(atlas.width)
        + static_cast<std::size_t>(x)
    ) * 4u;
    atlas.rgba_pixels[offset] = value;
    atlas.rgba_pixels[offset + 1u] = value;
    atlas.rgba_pixels[offset + 2u] = value;
    atlas.rgba_pixels[offset + 3u] = 255u;
}

}  // namespace

LightMapAtlas BuildVisibilityLightMapAtlas(
    const ModelGeometry& geometry,
    std::int32_t maximum_dimension
) {
    LightMapAtlas result;
    result.regions.resize(geometry.light_maps.size());
    if (!geometry.valid) {
        result.error = geometry.error.empty() ? "model geometry is invalid" : geometry.error;
        return result;
    }
    if (maximum_dimension < 64 || maximum_dimension > 16384) {
        result.error = "light-map atlas maximum dimension is unreasonable";
        return result;
    }

    try {
        std::set<std::size_t> referenced;
        for (const BspTriangle& triangle : geometry.triangles) {
            if (triangle.surface_index < 0
                || static_cast<std::size_t>(triangle.surface_index) >= geometry.surfaces.size()) {
                continue;
            }
            const std::int32_t light_map = geometry.surfaces[
                static_cast<std::size_t>(triangle.surface_index)
            ].light_map_index;
            if (light_map >= 0 && static_cast<std::size_t>(light_map) < geometry.light_maps.size()) {
                referenced.insert(static_cast<std::size_t>(light_map));
            }
        }
        result.referenced_light_maps = referenced.size();
        if (referenced.empty()) {
            result.error = "model BSP has no referenced light maps";
            return result;
        }

        std::vector<Patch> patches;
        patches.reserve(referenced.size());
        for (const std::size_t index : referenced) {
            const LightMapIndex& light_map = geometry.light_maps[index];
            patches.push_back({index, light_map.u_clamp, light_map.v_clamp});
        }
        std::sort(patches.begin(), patches.end(), [](const Patch& left, const Patch& right) {
            if (left.height != right.height) {
                return left.height > right.height;
            }
            return left.width > right.width;
        });

        std::int32_t used_height = 0;
        bool packed = false;
        for (std::int32_t candidate = 256; candidate <= maximum_dimension; candidate *= 2) {
            std::fill(result.regions.begin(), result.regions.end(), LightMapAtlasRegion{});
            if (PackPatches(patches, candidate, maximum_dimension, result.regions, used_height)) {
                result.width = candidate;
                packed = true;
                break;
            }
            if (candidate > maximum_dimension / 2) {
                break;
            }
        }
        if (!packed) {
            throw std::runtime_error("referenced light maps do not fit in the bounded atlas");
        }
        result.height = NextPowerOfTwo(used_height);
        if (result.height > maximum_dimension) {
            throw std::runtime_error("light-map atlas height exceeds the configured limit");
        }
        const std::uint64_t atlas_bytes = static_cast<std::uint64_t>(result.width)
            * static_cast<std::uint64_t>(result.height) * 4u;
        if (atlas_bytes > kMaxAtlasBytes) {
            throw std::runtime_error("light-map atlas exceeds the memory limit");
        }
        result.rgba_pixels.assign(static_cast<std::size_t>(atlas_bytes), 255u);

        for (const Patch& patch : patches) {
            const LightMapIndex& light_map = geometry.light_maps[patch.light_map_index];
            const LightMapAtlasRegion& region = result.regions[patch.light_map_index];
            const std::size_t pixel_count = static_cast<std::size_t>(patch.width)
                * static_cast<std::size_t>(patch.height);
            std::vector<float> visibility(pixel_count, 0.0f);
            const std::size_t light_count = CountLightActors(geometry, light_map);
            result.shadow_mask_count += light_count;
            for (std::size_t light = 0; light < light_count; ++light) {
                const std::vector<float> mask = DecodeBlurredMask(geometry, light_map, light);
                for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
                    visibility[pixel] += mask[pixel];
                }
            }
            for (std::int32_t y = 0; y < patch.height; ++y) {
                for (std::int32_t x = 0; x < patch.width; ++x) {
                    const std::size_t pixel = static_cast<std::size_t>(y)
                        * static_cast<std::size_t>(patch.width) + static_cast<std::size_t>(x);
                    const float brightness = light_count == 0
                        ? 0.78f
                        : 0.24f + 0.76f * std::min(visibility[pixel], 1.0f);
                    const auto value = static_cast<std::uint8_t>(std::clamp(
                        std::lround(brightness * 255.0f), 0l, 255l
                    ));
                    WritePixel(result, region.x + x, region.y + y, value);
                }
            }

            for (std::int32_t x = 0; x < patch.width; ++x) {
                const std::size_t top = static_cast<std::size_t>(x);
                const std::size_t bottom = static_cast<std::size_t>(patch.height - 1)
                    * static_cast<std::size_t>(patch.width) + static_cast<std::size_t>(x);
                const float top_brightness = light_count == 0
                    ? 0.78f : 0.24f + 0.76f * std::min(visibility[top], 1.0f);
                const float bottom_brightness = light_count == 0
                    ? 0.78f : 0.24f + 0.76f * std::min(visibility[bottom], 1.0f);
                WritePixel(result, region.x + x, region.y - 1,
                           static_cast<std::uint8_t>(std::lround(top_brightness * 255.0f)));
                WritePixel(result, region.x + x, region.y + patch.height,
                           static_cast<std::uint8_t>(std::lround(bottom_brightness * 255.0f)));
            }
            for (std::int32_t y = -1; y <= patch.height; ++y) {
                const std::int32_t source_y = std::clamp(y, 0, patch.height - 1);
                const std::size_t left = static_cast<std::size_t>(source_y)
                    * static_cast<std::size_t>(patch.width);
                const std::size_t right = left + static_cast<std::size_t>(patch.width - 1);
                const float left_brightness = light_count == 0
                    ? 0.78f : 0.24f + 0.76f * std::min(visibility[left], 1.0f);
                const float right_brightness = light_count == 0
                    ? 0.78f : 0.24f + 0.76f * std::min(visibility[right], 1.0f);
                WritePixel(result, region.x - 1, region.y + y,
                           static_cast<std::uint8_t>(std::lround(left_brightness * 255.0f)));
                WritePixel(result, region.x + patch.width, region.y + y,
                           static_cast<std::uint8_t>(std::lround(right_brightness * 255.0f)));
            }
        }

        std::set<std::int32_t> lit_surface_indices;
        for (const BspTriangle& triangle : geometry.triangles) {
            if (triangle.surface_index < 0
                || static_cast<std::size_t>(triangle.surface_index) >= geometry.surfaces.size()) {
                continue;
            }
            const std::int32_t light_map = geometry.surfaces[
                static_cast<std::size_t>(triangle.surface_index)
            ].light_map_index;
            if (light_map >= 0 && static_cast<std::size_t>(light_map) < result.regions.size()
                && result.regions[static_cast<std::size_t>(light_map)].valid) {
                lit_surface_indices.insert(triangle.surface_index);
                ++result.lit_triangles;
            }
        }
        result.lit_surfaces = lit_surface_indices.size();
        result.valid = result.lit_triangles > 0 && !result.rgba_pixels.empty();
        if (!result.valid) {
            result.error = "light-map atlas contains no BSP triangles";
            result.rgba_pixels.clear();
        }
    } catch (const std::exception& exception) {
        result.error = exception.what();
        result.rgba_pixels.clear();
        result.valid = false;
    }
    return result;
}

bool ComputeSurfaceLightMapCoordinate(
    const ModelGeometry& geometry,
    const LightMapAtlas& atlas,
    std::int32_t surface_index,
    std::uint32_t point_index,
    TextureCoordinate& coordinate
) {
    if (!atlas.valid || atlas.width <= 0 || atlas.height <= 0
        || surface_index < 0 || static_cast<std::size_t>(surface_index) >= geometry.surfaces.size()
        || point_index >= geometry.points.size()) {
        return false;
    }
    const BspSurface& surface = geometry.surfaces[static_cast<std::size_t>(surface_index)];
    if (surface.light_map_index < 0
        || static_cast<std::size_t>(surface.light_map_index) >= geometry.light_maps.size()
        || static_cast<std::size_t>(surface.light_map_index) >= atlas.regions.size()
        || surface.base_point_index < 0
        || static_cast<std::size_t>(surface.base_point_index) >= geometry.points.size()
        || surface.texture_u_vector_index < 0
        || static_cast<std::size_t>(surface.texture_u_vector_index) >= geometry.vectors.size()
        || surface.texture_v_vector_index < 0
        || static_cast<std::size_t>(surface.texture_v_vector_index) >= geometry.vectors.size()) {
        return false;
    }
    const LightMapIndex& light_map = geometry.light_maps[
        static_cast<std::size_t>(surface.light_map_index)
    ];
    const LightMapAtlasRegion& region = atlas.regions[
        static_cast<std::size_t>(surface.light_map_index)
    ];
    if (!region.valid) {
        return false;
    }

    const Vec3& point = geometry.points[point_index];
    const Vec3& base = geometry.points[static_cast<std::size_t>(surface.base_point_index)];
    const Vec3& texture_u = geometry.vectors[
        static_cast<std::size_t>(surface.texture_u_vector_index)
    ];
    const Vec3& texture_v = geometry.vectors[
        static_cast<std::size_t>(surface.texture_v_vector_index)
    ];
    const double point_u = static_cast<double>(point.x) * texture_u.x
        + static_cast<double>(point.y) * texture_u.y
        + static_cast<double>(point.z) * texture_u.z;
    const double point_v = static_cast<double>(point.x) * texture_v.x
        + static_cast<double>(point.y) * texture_v.y
        + static_cast<double>(point.z) * texture_v.z;
    const double base_u = static_cast<double>(base.x) * texture_u.x
        + static_cast<double>(base.y) * texture_u.y
        + static_cast<double>(base.z) * texture_u.z;
    const double base_v = static_cast<double>(base.x) * texture_v.x
        + static_cast<double>(base.y) * texture_v.y
        + static_cast<double>(base.z) * texture_v.z;
    double source_u = (point_u - (base_u + light_map.pan.x - 0.5 * light_map.u_scale))
        / light_map.u_scale;
    double source_v = (point_v - (base_v + light_map.pan.y - 0.5 * light_map.v_scale))
        / light_map.v_scale;
    if (!std::isfinite(source_u) || !std::isfinite(source_v)) {
        return false;
    }
    source_u = std::clamp(source_u, 0.5, static_cast<double>(region.width) - 0.5);
    source_v = std::clamp(source_v, 0.5, static_cast<double>(region.height) - 0.5);
    coordinate.u = static_cast<float>((static_cast<double>(region.x) + source_u)
                                      / static_cast<double>(atlas.width));
    coordinate.v = static_cast<float>((static_cast<double>(region.y) + source_v)
                                      / static_cast<double>(atlas.height));
    return std::isfinite(coordinate.u) && std::isfinite(coordinate.v);
}

}  // namespace hp2
