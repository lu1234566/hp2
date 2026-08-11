#include "hp2/ue_model.h"
#include "hp2/ue_lightmap.h"
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
    const bool texture_probe_requested = argc == 3;
    if (texture_probe_requested && model.valid) {
        texture = hp2::LoadFirstSurfaceTexture(argv[2], package, model);
        textures = hp2::LoadSurfaceTextures(argv[2], package, model);
        light_maps = hp2::BuildVisibilityLightMapAtlas(model);
    } else if (texture_probe_requested) {
        texture.error = "model geometry is invalid";
    }

    std::cout << "{\n"
              << "  \"schema\": \"hp2-map-index-v3\",\n"
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
    return texture_probe_requested && !light_maps.valid ? 6 : 0;
}
