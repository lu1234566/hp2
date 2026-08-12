#pragma once

#include "hp2/ue_actor.h"
#include "hp2/ue_texture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace hp2 {

struct VertexMeshTriangle {
    std::array<std::uint32_t, 3> indices{};
    std::array<TextureCoordinate, 3> texture_coordinates{};
    std::int32_t texture_slot = 0;
};

struct DecodedVertexMesh {
    bool valid = false;
    std::filesystem::path package_path;
    std::string package_name;
    std::string object_name;
    std::string class_name;
    std::vector<Vec3> vertices;
    std::vector<VertexMeshTriangle> triangles;
    std::vector<std::int32_t> texture_references;
    Vec3 scale{1.0f, 1.0f, 1.0f};
    Vec3 origin;
    Rotator rotation_origin;
    std::int32_t frame_vertices = 0;
    std::int32_t animation_frames = 0;
    std::size_t skeletal_points = 0;
    std::size_t skeletal_bones = 0;
    std::string error;
};

struct ActorMeshMaterial {
    std::int32_t source_reference = 0;
    DecodedTexture texture;
};

struct ActorMeshTriangle {
    std::array<Vec3, 3> points{};
    std::array<TextureCoordinate, 3> texture_coordinates{};
    std::int32_t material_index = -1;
};

struct ActorMeshAssetSummary {
    std::string package_name;
    std::string object_name;
    std::string class_name;
    std::size_t vertices = 0;
    std::size_t triangles = 0;
    std::size_t texture_slots = 0;
    std::size_t skeletal_points = 0;
    std::size_t skeletal_bones = 0;
    bool vertex_bounds_valid = false;
    Vec3 vertex_bounds_min;
    Vec3 vertex_bounds_max;
    Vec3 mesh_scale{1.0f, 1.0f, 1.0f};
    Vec3 mesh_origin;
};

struct ActorMeshInstanceSummary {
    std::string actor_object_name;
    std::string actor_class_name;
    std::string mesh_package_name;
    std::string mesh_object_name;
    bool inherited_mesh = false;
    bool has_location = false;
    Vec3 location;
    bool has_pre_pivot = false;
    Vec3 pre_pivot;
    bool has_draw_scale = false;
    float draw_scale = 1.0f;
    bool has_draw_scale_3d = false;
    Vec3 draw_scale_3d{1.0f, 1.0f, 1.0f};
    std::size_t source_triangles = 0;
    std::size_t emitted_triangles = 0;
    bool bounds_valid = false;
    Vec3 bounds_min;
    Vec3 bounds_max;
};

struct ActorMeshScene {
    bool valid = false;
    bool bounds_valid = false;
    std::size_t candidate_instances = 0;
    std::size_t direct_mesh_candidates = 0;
    std::size_t inherited_mesh_candidates = 0;
    std::size_t class_exports_scanned = 0;
    std::size_t class_default_streams_found = 0;
    std::size_t class_default_scan_misses = 0;
    std::size_t class_resolution_failures = 0;
    std::size_t decoded_mesh_assets = 0;
    std::size_t decoded_mesh_instances = 0;
    std::size_t decoded_inherited_mesh_instances = 0;
    std::size_t failed_mesh_instances = 0;
    std::size_t source_triangles = 0;
    std::size_t textured_triangles = 0;
    std::size_t decoded_materials = 0;
    std::size_t failed_materials = 0;
    Vec3 bounds_min;
    Vec3 bounds_max;
    std::vector<ActorMeshAssetSummary> assets;
    std::vector<ActorMeshInstanceSummary> instances;
    std::vector<ActorMeshMaterial> materials;
    std::vector<ActorMeshTriangle> triangles;
    std::string error;
};

DecodedVertexMesh LoadVertexMeshExport(
    const PackageIndex& package,
    std::size_t export_index
);

ActorMeshScene LoadDirectActorMeshes(
    const std::filesystem::path& game_root,
    const PackageIndex& map_package,
    const LevelActorCensus& actors,
    std::size_t max_triangles = 300'000
);

}  // namespace hp2
