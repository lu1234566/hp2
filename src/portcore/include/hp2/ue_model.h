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
