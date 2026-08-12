#pragma once

#include "hp2/ue_model.h"
#include "hp2/ue_texture.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hp2 {

struct LightMapAtlasRegion {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
    bool valid = false;
};

struct LightMapAtlas {
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::vector<std::uint8_t> rgba_pixels;
    std::vector<LightMapAtlasRegion> regions;
    std::size_t referenced_light_maps = 0;
    std::size_t shadow_mask_count = 0;
    std::size_t lit_surfaces = 0;
    std::size_t lit_triangles = 0;
    bool valid = false;
    std::string error;
};

LightMapAtlas BuildVisibilityLightMapAtlas(
    const ModelGeometry& geometry,
    std::int32_t maximum_dimension = 4096
);

bool ComputeSurfaceLightMapCoordinate(
    const ModelGeometry& geometry,
    const LightMapAtlas& atlas,
    std::int32_t surface_index,
    std::uint32_t point_index,
    TextureCoordinate& coordinate
);

}  // namespace hp2
