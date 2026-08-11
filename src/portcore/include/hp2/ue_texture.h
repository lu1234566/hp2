#pragma once

#include "hp2/ue_model.h"
#include "hp2/ue_package.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace hp2 {

struct TextureCoordinate {
    float u = 0.0f;
    float v = 0.0f;
};

struct DecodedTexture {
    std::int32_t map_material_index = 0;
    std::size_t triangle_count = 0;
    std::filesystem::path package_path;
    std::string package_name;
    std::string object_name;
    std::string palette_name;
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::vector<std::uint8_t> rgba_pixels;
    bool valid = false;
    std::string error;
};

struct DecodedTextureSet {
    std::vector<DecodedTexture> textures;
    std::size_t material_candidates = 0;
    std::size_t textured_triangles = 0;
    std::size_t failed_materials = 0;
    std::size_t rgba_bytes = 0;
    bool valid = false;
    std::string error;
};

bool ComputeSurfaceTextureCoordinate(
    const ModelGeometry& geometry,
    std::int32_t surface_index,
    std::uint32_t point_index,
    std::int32_t texture_width,
    std::int32_t texture_height,
    TextureCoordinate& coordinate
);

DecodedTexture LoadTextureExport(const PackageIndex& package, std::size_t export_index);

DecodedTexture LoadFirstSurfaceTexture(
    const std::filesystem::path& game_root,
    const PackageIndex& map_package,
    const ModelGeometry& geometry
);

DecodedTextureSet LoadSurfaceTextures(
    const std::filesystem::path& game_root,
    const PackageIndex& map_package,
    const ModelGeometry& geometry
);

DecodedTexture LoadFirstSurfaceTexture(
    const std::filesystem::path& game_root,
    const std::filesystem::path& map_path,
    const ModelGeometry& geometry
);

}  // namespace hp2
