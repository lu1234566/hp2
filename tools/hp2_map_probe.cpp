#include "hp2/ue_actor.h"
#include "hp2/ue_model.h"
#include "hp2/ue_lightmap.h"
#include "hp2/ue_mesh.h"
#include "hp2/ue_package.h"
#include "hp2/ue_texture.h"

#include <cstddef>
#include <filesystem>
#include <iostream>
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

void PrintExport(const hp2::ExportEntry& entry, std::size_t index, const char* indentation) {
    std::cout << indentation
              << "{\"index\": " << index
              << ", \"object_name\": \"" << JsonEscape(entry.object_name)
              << "\", \"class_name\": \"" << JsonEscape(entry.class_name)
              << "\", \"class_index\": " << entry.class_index
              << ", \"super_index\": " << entry.super_index
              << ", \"outer_index\": " << entry.package_index
              << ", \"object_flags\": " << entry.object_flags
              << ", \"serial_size\": " << entry.serial_size
              << ", \"serial_offset\": " << entry.serial_offset
              << "}";
}

struct ReferenceLabel {
    std::string package_name;
    std::string object_name;
    std::string class_name;
};

ReferenceLabel ResolveReferenceLabel(const hp2::PackageIndex& package, std::int32_t reference) {
    ReferenceLabel result;
    if (reference > 0) {
        const std::int64_t index = static_cast<std::int64_t>(reference) - 1;
        if (index >= 0 && static_cast<std::size_t>(index) < package.exports.size()) {
            const auto& entry = package.exports[static_cast<std::size_t>(index)];
            result.package_name = package.summary.path.stem().string();
            result.object_name = entry.object_name;
            result.class_name = entry.class_name;
        }
        return result;
    }
    if (reference >= 0) {
        return result;
    }
    const std::int64_t index = -static_cast<std::int64_t>(reference) - 1;
    if (index < 0 || static_cast<std::size_t>(index) >= package.imports.size()) {
        return result;
    }
    const auto& leaf = package.imports[static_cast<std::size_t>(index)];
    result.object_name = leaf.object_name;
    result.class_name = leaf.class_name;
    std::int32_t outer = leaf.package_index;
    for (std::size_t depth = 0; outer < 0 && depth < 64; ++depth) {
        const std::int64_t outer_index = -static_cast<std::int64_t>(outer) - 1;
        if (outer_index < 0 || static_cast<std::size_t>(outer_index) >= package.imports.size()) {
            break;
        }
        const auto& outer_entry = package.imports[static_cast<std::size_t>(outer_index)];
        result.package_name = outer_entry.object_name;
        outer = outer_entry.package_index;
    }
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) {
        std::cerr << "Usage: hp2_map_probe <map.unr> [game-root]\n";
        return 64;
    }

    const std::filesystem::path map_path = argv[1];
    const hp2::PackageIndex package = hp2::LoadPackageIndex(map_path);
    if (!package.valid) {
        std::cerr << "Cannot parse map package index: " << package.error << '\n';
        return 2;
    }

    std::size_t geometry_count = 0;
    for (const auto& entry : package.exports) {
        geometry_count += hp2::IsGeometryCandidate(entry) ? 1u : 0u;
    }

    const std::size_t model_export_index = hp2::FindLargestModelExport(package);
    hp2::ModelGeometry model;
    if (model_export_index < package.exports.size()) {
        model = hp2::LoadModelGeometry(package, model_export_index);
    } else {
        model.error = "package contains no serialized Model export";
    }

    hp2::DecodedTexture texture;
    hp2::DecodedTextureSet textures;
    hp2::LightMapAtlas light_maps;
    const hp2::LevelActorCensus actors = hp2::LoadLevelActorCensus(package);
    hp2::ActorMeshScene actor_meshes;
    const bool texture_probe_requested = argc == 3;
    if (texture_probe_requested && model.valid) {
        texture = hp2::LoadFirstSurfaceTexture(argv[2], package, model);
        textures = hp2::LoadSurfaceTextures(argv[2], package, model);
        light_maps = hp2::BuildVisibilityLightMapAtlas(model);
        if (actors.valid) {
            actor_meshes = hp2::LoadDirectActorMeshes(argv[2], package, actors);
        }
    } else if (texture_probe_requested) {
        texture.error = "model geometry is invalid";
    }

    std::cout << "{\n"
              << "  \"schema\": \"hp2-map-index-v5\",\n"
              << "  \"path\": \"" << JsonEscape(map_path.generic_string()) << "\",\n"
              << "  \"version\": " << package.summary.file_version << ",\n"
              << "  \"licensee_version\": " << package.summary.licensee_version << ",\n"
              << "  \"name_count\": " << package.names.size() << ",\n"
              << "  \"import_count\": " << package.imports.size() << ",\n"
              << "  \"export_count\": " << package.exports.size() << ",\n"
              << "  \"geometry_candidate_count\": " << geometry_count << ",\n"
              << "  \"model_geometry\": {\n"
              << "    \"valid\": " << (model.valid ? "true" : "false") << ",\n"
              << "    \"export_index\": "
              << (model_export_index < package.exports.size()
                      ? std::to_string(model_export_index)
                      : std::string("null")) << ",\n"
              << "    \"object_name\": \"" << JsonEscape(model.object_name) << "\",\n"
              << "    \"payload_bytes_consumed\": " << model.payload_bytes_consumed << ",\n"
              << "    \"vectors\": " << model.vectors.size() << ",\n"
              << "    \"points\": " << model.points.size() << ",\n"
              << "    \"nodes\": " << model.nodes.size() << ",\n"
              << "    \"surfaces\": " << model.surfaces.size() << ",\n"
              << "    \"vertices\": " << model.vertices.size() << ",\n"
              << "    \"triangles\": " << model.triangles.size() << ",\n"
              << "    \"skipped_nodes\": " << model.skipped_nodes << ",\n"
              << "    \"skipped_triangles\": " << model.skipped_triangles << ",\n"
              << "    \"bounds_min\": [" << model.bounds_min.x << ", "
              << model.bounds_min.y << ", " << model.bounds_min.z << "],\n"
              << "    \"bounds_max\": [" << model.bounds_max.x << ", "
              << model.bounds_max.y << ", " << model.bounds_max.z << "]";
    if (!model.error.empty()) {
        std::cout << ",\n    \"error\": \"" << JsonEscape(model.error) << "\"\n";
    } else {
        std::cout << "\n";
    }
    std::cout << "  },\n"
              << "  \"g3_texture\": {\n"
              << "    \"requested\": " << (texture_probe_requested ? "true" : "false") << ",\n"
              << "    \"valid\": " << (texture.valid ? "true" : "false") << ",\n"
              << "    \"material_index\": " << texture.map_material_index << ",\n"
              << "    \"triangle_count\": " << texture.triangle_count << ",\n"
              << "    \"package_name\": \"" << JsonEscape(texture.package_name) << "\",\n"
              << "    \"object_name\": \"" << JsonEscape(texture.object_name) << "\",\n"
              << "    \"palette_name\": \"" << JsonEscape(texture.palette_name) << "\",\n"
              << "    \"width\": " << texture.width << ",\n"
              << "    \"height\": " << texture.height << ",\n"
              << "    \"rgba_bytes\": " << texture.rgba_pixels.size();
    if (!texture.error.empty()) {
        std::cout << ",\n    \"error\": \"" << JsonEscape(texture.error) << "\"\n";
    } else {
        std::cout << "\n";
    }
    std::cout << "  },\n"
              << "  \"g4_scene\": {\n"
              << "    \"requested\": " << (texture_probe_requested ? "true" : "false") << ",\n"
              << "    \"texture_set_valid\": " << (textures.valid ? "true" : "false") << ",\n"
              << "    \"material_candidates\": " << textures.material_candidates << ",\n"
              << "    \"decoded_textures\": " << textures.textures.size() << ",\n"
              << "    \"failed_materials\": " << textures.failed_materials << ",\n"
              << "    \"textured_triangles\": " << textures.textured_triangles << ",\n"
              << "    \"texture_rgba_bytes\": " << textures.rgba_bytes << ",\n"
              << "    \"lightmap_valid\": " << (light_maps.valid ? "true" : "false") << ",\n"
              << "    \"model_lightmaps\": " << model.light_maps.size() << ",\n"
              << "    \"light_bits_bytes\": " << model.light_bits.size() << ",\n"
              << "    \"referenced_lightmaps\": " << light_maps.referenced_light_maps << ",\n"
              << "    \"shadow_masks\": " << light_maps.shadow_mask_count << ",\n"
              << "    \"lit_surfaces\": " << light_maps.lit_surfaces << ",\n"
              << "    \"lit_triangles\": " << light_maps.lit_triangles << ",\n"
              << "    \"atlas_width\": " << light_maps.width << ",\n"
              << "    \"atlas_height\": " << light_maps.height;
    if (!textures.error.empty() || !light_maps.error.empty()) {
        std::cout << ",\n    \"texture_error\": \"" << JsonEscape(textures.error)
                  << "\",\n    \"lightmap_error\": \"" << JsonEscape(light_maps.error) << "\"\n";
    } else {
        std::cout << "\n";
    }
    std::cout << "  },\n"
              << "  \"g5_actor_census\": {\n"
              << "    \"valid\": " << (actors.valid ? "true" : "false") << ",\n"
              << "    \"level_export_index\": " << actors.level_export_index << ",\n"
              << "    \"level_object_name\": \"" << JsonEscape(actors.level_object_name) << "\",\n"
              << "    \"actor_references\": " << actors.actor_reference_count << ",\n"
              << "    \"non_null_actor_references\": " << actors.non_null_actor_references << ",\n"
              << "    \"parsed_actors\": " << actors.parsed_actor_count << ",\n"
              << "    \"parse_failures\": " << actors.actor_parse_failures << ",\n"
              << "    \"actors_with_location\": " << actors.actors_with_location << ",\n"
              << "    \"actors_with_rotation\": " << actors.actors_with_rotation << ",\n"
              << "    \"direct_mesh_references\": " << actors.direct_mesh_references << ",\n"
              << "    \"direct_static_mesh_references\": "
              << actors.direct_static_mesh_references << ",\n"
              << "    \"class_inventory\": [\n";
    for (std::size_t index = 0; index < actors.class_inventory.size(); ++index) {
        const auto& item = actors.class_inventory[index];
        std::cout << "      {\"class_name\": \"" << JsonEscape(item.class_name)
                  << "\", \"count\": " << item.count << "}"
                  << (index + 1 == actors.class_inventory.size() ? "" : ",") << '\n';
    }
    std::cout << "    ],\n    \"mesh_references\": [\n";
    std::size_t mesh_reference_count = 0;
    for (const auto& actor : actors.actors) {
        mesh_reference_count += actor.mesh_reference != 0 ? 1u : 0u;
        mesh_reference_count += actor.static_mesh_reference != 0 ? 1u : 0u;
    }
    std::size_t printed_mesh_references = 0;
    for (const auto& actor : actors.actors) {
        for (int property_index = 0; property_index < 2; ++property_index) {
            const std::int32_t reference = property_index == 0
                ? actor.mesh_reference : actor.static_mesh_reference;
            if (reference == 0) {
                continue;
            }
            const ReferenceLabel label = ResolveReferenceLabel(package, reference);
            std::cout << "      {\"actor_class\": \"" << JsonEscape(actor.class_name)
                      << "\", \"actor_object\": \"" << JsonEscape(actor.object_name)
                      << "\", \"property\": \""
                      << (property_index == 0 ? "Mesh" : "StaticMesh")
                      << "\", \"reference\": " << reference
                      << ", \"package_name\": \"" << JsonEscape(label.package_name)
                      << "\", \"object_name\": \"" << JsonEscape(label.object_name)
                      << "\", \"class_name\": \"" << JsonEscape(label.class_name) << "\"}"
                      << (++printed_mesh_references == mesh_reference_count ? "" : ",") << '\n';
        }
    }
    std::cout << "    ]";
    if (!actors.error.empty()) {
        std::cout << ",\n    \"error\": \"" << JsonEscape(actors.error) << "\"\n";
    } else {
        std::cout << "\n";
    }
    std::cout << "  },\n"
              << "  \"g5_mesh_scene\": {\n"
              << "    \"requested\": " << (texture_probe_requested ? "true" : "false") << ",\n"
              << "    \"valid\": " << (actor_meshes.valid ? "true" : "false") << ",\n"
              << "    \"candidate_instances\": " << actor_meshes.candidate_instances << ",\n"
              << "    \"direct_mesh_candidates\": "
              << actor_meshes.direct_mesh_candidates << ",\n"
              << "    \"inherited_mesh_candidates\": "
              << actor_meshes.inherited_mesh_candidates << ",\n"
              << "    \"class_exports_scanned\": "
              << actor_meshes.class_exports_scanned << ",\n"
              << "    \"class_default_streams_found\": "
              << actor_meshes.class_default_streams_found << ",\n"
              << "    \"class_default_scan_misses\": "
              << actor_meshes.class_default_scan_misses << ",\n"
              << "    \"class_resolution_failures\": "
              << actor_meshes.class_resolution_failures << ",\n"
              << "    \"decoded_mesh_assets\": " << actor_meshes.decoded_mesh_assets << ",\n"
              << "    \"decoded_mesh_instances\": " << actor_meshes.decoded_mesh_instances << ",\n"
              << "    \"decoded_inherited_mesh_instances\": "
              << actor_meshes.decoded_inherited_mesh_instances << ",\n"
              << "    \"failed_mesh_instances\": " << actor_meshes.failed_mesh_instances << ",\n"
              << "    \"source_triangles\": " << actor_meshes.source_triangles << ",\n"
              << "    \"textured_triangles\": " << actor_meshes.textured_triangles << ",\n"
              << "    \"decoded_materials\": " << actor_meshes.decoded_materials << ",\n"
              << "    \"failed_materials\": " << actor_meshes.failed_materials << ",\n"
              << "    \"bounds_valid\": " << (actor_meshes.bounds_valid ? "true" : "false") << ",\n"
              << "    \"bounds_min\": [" << actor_meshes.bounds_min.x << ", "
              << actor_meshes.bounds_min.y << ", " << actor_meshes.bounds_min.z << "],\n"
              << "    \"bounds_max\": [" << actor_meshes.bounds_max.x << ", "
              << actor_meshes.bounds_max.y << ", " << actor_meshes.bounds_max.z << "],\n"
              << "    \"assets\": [\n";
    for (std::size_t index = 0; index < actor_meshes.assets.size(); ++index) {
        const auto& asset = actor_meshes.assets[index];
        std::cout << "      {\"package_name\": \"" << JsonEscape(asset.package_name)
                  << "\", \"object_name\": \"" << JsonEscape(asset.object_name)
                  << "\", \"class_name\": \"" << JsonEscape(asset.class_name)
                  << "\", \"vertices\": " << asset.vertices
                  << ", \"triangles\": " << asset.triangles
                  << ", \"texture_slots\": " << asset.texture_slots
                  << ", \"skeletal_points\": " << asset.skeletal_points
                  << ", \"skeletal_bones\": " << asset.skeletal_bones << "}"
                  << (index + 1 == actor_meshes.assets.size() ? "" : ",") << '\n';
    }
    std::cout << "    ]";
    if (!actor_meshes.error.empty()) {
        std::cout << ",\n    \"error\": \"" << JsonEscape(actor_meshes.error) << "\"\n";
    } else {
        std::cout << "\n";
    }
    std::cout << "  },\n"
              << "  \"imports\": [\n";

    for (std::size_t index = 0; index < package.imports.size(); ++index) {
        const auto& entry = package.imports[index];
        std::cout << "    {\"index\": " << index
                  << ", \"class_package\": \"" << JsonEscape(entry.class_package)
                  << "\", \"class_name\": \"" << JsonEscape(entry.class_name)
                  << "\", \"outer_index\": " << entry.package_index
                  << ", \"object_name\": \"" << JsonEscape(entry.object_name) << "\"}"
                  << (index + 1 == package.imports.size() ? "" : ",") << '\n';
    }

    std::cout << "  ],\n  \"geometry_candidates\": [\n";
    std::size_t printed_geometry = 0;
    for (std::size_t index = 0; index < package.exports.size(); ++index) {
        if (!hp2::IsGeometryCandidate(package.exports[index])) {
            continue;
        }
        PrintExport(package.exports[index], index, "    ");
        ++printed_geometry;
        std::cout << (printed_geometry == geometry_count ? "" : ",") << '\n';
    }

    std::cout << "  ],\n  \"exports\": [\n";
    for (std::size_t index = 0; index < package.exports.size(); ++index) {
        PrintExport(package.exports[index], index, "    ");
        std::cout << (index + 1 == package.exports.size() ? "" : ",") << '\n';
    }
    std::cout << "  ]\n}\n";
    if (geometry_count == 0) {
        return 3;
    }
    if (!model.valid) {
        return 4;
    }
    if (texture_probe_requested && (!texture.valid || !textures.valid)) {
        return 5;
    }
    if (texture_probe_requested && !light_maps.valid) {
        return 6;
    }
    if (!actors.valid || actors.parsed_actor_count == 0) {
        return 7;
    }
    return texture_probe_requested && !actor_meshes.valid ? 8 : 0;
}
