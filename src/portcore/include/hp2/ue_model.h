#pragma once

#include "hp2/ue_package.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace hp2 {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct BspNode {
    std::int32_t vertex_pool_index = 0;
    std::int32_t surface_index = 0;
    std::int32_t back_index = -1;
    std::int32_t front_index = -1;
    std::int32_t plane_index = -1;
    std::int32_t back_zone = 0;
    std::int32_t front_zone = 0;
    std::uint8_t vertex_count = 0;
    std::uint8_t flags = 0;
};

struct BspSurface {
    std::int32_t material_index = 0;
    std::uint32_t poly_flags = 0;
    std::int32_t base_point_index = 0;
    std::int32_t normal_vector_index = 0;
    std::int32_t texture_u_vector_index = 0;
    std::int32_t texture_v_vector_index = 0;
    std::int32_t light_map_index = -1;
    std::int16_t pan_u = 0;
    std::int16_t pan_v = 0;
};

struct ModelZone {
    std::int32_t actor_reference = 0;
    std::uint64_t connectivity = 0;
    std::uint64_t visibility = 0;
};

struct LightMapIndex {
    std::int32_t data_offset = 0;
    Vec3 pan;
    std::int32_t u_clamp = 0;
    std::int32_t v_clamp = 0;
    float u_scale = 0.0f;
    float v_scale = 0.0f;
    std::int32_t light_actors = -1;
};

struct BspVertex {
    std::int32_t point_index = 0;
    std::int32_t side_index = -1;
};

struct BspTriangle {
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    std::uint32_t c = 0;
    std::uint32_t node_index = 0;
    std::int32_t surface_index = 0;
};

struct ModelGeometry {
    std::size_t export_index = std::numeric_limits<std::size_t>::max();
    std::string object_name;
    std::size_t payload_bytes_consumed = 0;
    std::vector<Vec3> vectors;
    std::vector<Vec3> points;
    std::vector<BspNode> nodes;
    std::vector<BspSurface> surfaces;
    std::vector<BspVertex> vertices;
    std::vector<ModelZone> zones;
    std::vector<LightMapIndex> light_maps;
    std::vector<std::uint8_t> light_bits;
    std::vector<std::int32_t> light_actor_references;
    std::vector<BspTriangle> triangles;
    Vec3 bounds_min;
    Vec3 bounds_max;
    std::size_t skipped_nodes = 0;
    std::size_t skipped_triangles = 0;
    bool valid = false;
    std::string error;
};

std::size_t FindLargestModelExport(const PackageIndex& package);
ModelGeometry LoadModelGeometry(const PackageIndex& package, std::size_t export_index);
ModelGeometry LoadPrimaryModelGeometry(const std::filesystem::path& map_path);

}  // namespace hp2
