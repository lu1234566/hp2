#include "hp2/runtime.h"
#include "hp2/ue_actor.h"
#include "hp2/ue_lightmap.h"
#include "hp2/ue_mesh.h"
#include "hp2/ue_model.h"
#include "hp2/ue_package.h"
#include "hp2/ue_texture.h"

#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void WriteU16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    bytes.at(offset) = static_cast<std::uint8_t>(value & 0xffu);
    bytes.at(offset + 1) = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
}

void WriteU32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        bytes.at(offset + index) = static_cast<std::uint8_t>((value >> (index * 8u)) & 0xffu);
    }
}

void AppendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        bytes.push_back(static_cast<std::uint8_t>((value >> (index * 8u)) & 0xffu));
    }
}

void AppendU16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
}

void AppendU64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        bytes.push_back(static_cast<std::uint8_t>((value >> (index * 8u)) & 0xffu));
    }
}

void AppendF32(std::vector<std::uint8_t>& bytes, float value) {
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    AppendU32(bytes, bits);
}

void AppendVec3(std::vector<std::uint8_t>& bytes, float x, float y, float z) {
    AppendF32(bytes, x);
    AppendF32(bytes, y);
    AppendF32(bytes, z);
}

void AppendCompactIndex(std::vector<std::uint8_t>& bytes, std::int32_t value) {
    const bool negative = value < 0;
    std::uint32_t magnitude = negative
        ? static_cast<std::uint32_t>(-static_cast<std::int64_t>(value))
        : static_cast<std::uint32_t>(value);
    std::uint8_t first = static_cast<std::uint8_t>(magnitude & 0x3fu);
    magnitude >>= 6u;
    if (negative) {
        first |= 0x80u;
    }
    if (magnitude != 0) {
        first |= 0x40u;
    }
    bytes.push_back(first);
    while (magnitude != 0) {
        std::uint8_t next = static_cast<std::uint8_t>(magnitude & 0x7fu);
        magnitude >>= 7u;
        if (magnitude != 0) {
            next |= 0x80u;
        }
        bytes.push_back(next);
    }
}

void AppendName(std::vector<std::uint8_t>& bytes, const std::string& value) {
    AppendCompactIndex(bytes, static_cast<std::int32_t>(value.size() + 1));
    bytes.insert(bytes.end(), value.begin(), value.end());
    bytes.push_back(0);
    AppendU32(bytes, 0);
}

void AppendExportRecord(
    std::vector<std::uint8_t>& bytes,
    std::int32_t class_index,
    std::int32_t object_name_index,
    std::int32_t serial_size,
    std::int32_t serial_offset,
    std::uint32_t object_flags = 1u
) {
    AppendCompactIndex(bytes, class_index);
    AppendCompactIndex(bytes, 0);  // superclass
    AppendU32(bytes, 0);           // outer
    AppendCompactIndex(bytes, object_name_index);
    AppendU32(bytes, object_flags);
    AppendCompactIndex(bytes, serial_size);
    AppendCompactIndex(bytes, serial_offset);
}

void WriteSyntheticActorPackage(
    const std::filesystem::path& path,
    const std::string& class_package_name = "ActorClasses",
    bool has_direct_draw_scale = false,
    float direct_draw_scale = 1.0f
) {
    std::vector<std::uint8_t> bytes(64, 0);
    WriteU32(bytes, 0, hp2::kUnrealPackageTag);
    WriteU16(bytes, 4, 79);

    const std::vector<std::string> names = {
        "None", "Core", "Class", "Level", "Decoration", "MyLevel", "Chair0",
        "Location", "Vector", "Rotation", "Rotator", "DrawScale3D", "PrePivot",
        "bHidden", "Package", class_package_name, "DrawScale"
    };
    const std::size_t name_offset = bytes.size();
    for (const auto& name : names) {
        AppendName(bytes, name);
    }

    const std::size_t import_offset = bytes.size();
    AppendCompactIndex(bytes, 1);   // Core
    AppendCompactIndex(bytes, 2);   // Class
    AppendU32(bytes, 0);
    AppendCompactIndex(bytes, 3);   // Level
    AppendCompactIndex(bytes, 1);   // Core
    AppendCompactIndex(bytes, 14);  // Package
    AppendU32(bytes, 0);
    AppendCompactIndex(bytes, 15);  // ActorClasses
    AppendCompactIndex(bytes, 1);   // Core
    AppendCompactIndex(bytes, 2);   // Class
    AppendU32(bytes, 0xfffffffeu);  // ActorClasses import (-2)
    AppendCompactIndex(bytes, 4);   // Decoration

    std::vector<std::uint8_t> level_payload;
    AppendCompactIndex(level_payload, 0);  // None
    AppendU32(level_payload, 1);           // actor count
    AppendU32(level_payload, 1);           // actor capacity
    AppendCompactIndex(level_payload, 2);  // second export

    std::vector<std::uint8_t> actor_payload;
    AppendCompactIndex(actor_payload, 0);  // StateFrame node
    AppendCompactIndex(actor_payload, 0);  // StateFrame state node
    AppendU64(actor_payload, 0);           // probe mask
    AppendU32(actor_payload, 0);           // latent action

    AppendCompactIndex(actor_payload, 7);  // Location
    actor_payload.push_back(0x3au);        // Struct, 12 bytes
    AppendCompactIndex(actor_payload, 8);  // Vector
    AppendVec3(actor_payload, 10.0f, 20.0f, 30.0f);

    AppendCompactIndex(actor_payload, 9);   // Rotation
    actor_payload.push_back(0x3au);         // Struct, 12 bytes
    AppendCompactIndex(actor_payload, 10);  // Rotator
    AppendU32(actor_payload, 1024);
    AppendU32(actor_payload, 2048);
    AppendU32(actor_payload, 4096);

    AppendCompactIndex(actor_payload, 11);  // DrawScale3D
    actor_payload.push_back(0x3au);
    AppendCompactIndex(actor_payload, 8);   // Vector
    AppendVec3(actor_payload, 1.0f, 2.0f, 3.0f);

    AppendCompactIndex(actor_payload, 12);  // PrePivot
    actor_payload.push_back(0x3au);
    AppendCompactIndex(actor_payload, 8);   // Vector
    AppendVec3(actor_payload, 4.0f, 5.0f, 6.0f);

    AppendCompactIndex(actor_payload, 13);  // bHidden
    actor_payload.push_back(0x03u);         // Bool false, no payload
    if (has_direct_draw_scale) {
        AppendCompactIndex(actor_payload, 16);  // DrawScale
        actor_payload.push_back(0x24u);         // Float, four bytes
        AppendF32(actor_payload, direct_draw_scale);
    }
    AppendCompactIndex(actor_payload, 0);   // None

    const std::size_t export_offset = bytes.size();
    std::size_t table_size = 32;
    std::vector<std::uint8_t> export_table;
    for (int attempt = 0; attempt < 8; ++attempt) {
        const auto level_offset = static_cast<std::int32_t>(export_offset + table_size);
        const auto actor_offset = level_offset + static_cast<std::int32_t>(level_payload.size());
        export_table.clear();
        AppendExportRecord(
            export_table, -1, 5, static_cast<std::int32_t>(level_payload.size()), level_offset
        );
        AppendExportRecord(
            export_table, -3, 6, static_cast<std::int32_t>(actor_payload.size()), actor_offset,
            0x02000001u
        );
        if (export_table.size() == table_size) {
            break;
        }
        table_size = export_table.size();
    }
    bytes.insert(bytes.end(), export_table.begin(), export_table.end());
    bytes.insert(bytes.end(), level_payload.begin(), level_payload.end());
    bytes.insert(bytes.end(), actor_payload.begin(), actor_payload.end());

    WriteU32(bytes, 12, static_cast<std::uint32_t>(names.size()));
    WriteU32(bytes, 16, static_cast<std::uint32_t>(name_offset));
    WriteU32(bytes, 20, 2);
    WriteU32(bytes, 24, static_cast<std::uint32_t>(export_offset));
    WriteU32(bytes, 28, 3);
    WriteU32(bytes, 32, static_cast<std::uint32_t>(import_offset));

    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void WriteSyntheticClassPackage(
    const std::filesystem::path& path,
    float draw_scale = 1.25f
) {
    std::vector<std::uint8_t> bytes(64, 0);
    WriteU32(bytes, 0, hp2::kUnrealPackageTag);
    WriteU16(bytes, 4, 79);

    const std::vector<std::string> names = {
        "None", "Core", "Class", "Package", "Decoration", "MeshPack",
        "LodMesh", "ChairMesh", "Mesh", "DrawScale"
    };
    const std::size_t name_offset = bytes.size();
    for (const auto& name : names) {
        AppendName(bytes, name);
    }

    const std::size_t import_offset = bytes.size();
    AppendCompactIndex(bytes, 1);   // Core
    AppendCompactIndex(bytes, 3);   // Package
    AppendU32(bytes, 0);
    AppendCompactIndex(bytes, 5);   // MeshPack
    AppendCompactIndex(bytes, 1);   // Core
    AppendCompactIndex(bytes, 6);   // LodMesh
    AppendU32(bytes, 0xffffffffu);  // MeshPack import (-1)
    AppendCompactIndex(bytes, 7);   // ChairMesh

    std::vector<std::uint8_t> class_payload(48u, 0x51u);
    AppendCompactIndex(class_payload, 8);   // Mesh
    class_payload.push_back(0x05u);         // Object, one serialized byte
    AppendCompactIndex(class_payload, -2);  // ChairMesh import
    AppendCompactIndex(class_payload, 9);   // DrawScale
    class_payload.push_back(0x24u);         // Float, four bytes
    AppendF32(class_payload, draw_scale);
    AppendCompactIndex(class_payload, 0);   // None

    const std::size_t export_offset = bytes.size();
    std::size_t table_size = 16;
    std::vector<std::uint8_t> export_table;
    for (int attempt = 0; attempt < 8; ++attempt) {
        const auto class_offset = static_cast<std::int32_t>(export_offset + table_size);
        export_table.clear();
        AppendExportRecord(
            export_table, 0, 4, static_cast<std::int32_t>(class_payload.size()), class_offset
        );
        if (export_table.size() == table_size) {
            break;
        }
        table_size = export_table.size();
    }
    bytes.insert(bytes.end(), export_table.begin(), export_table.end());
    bytes.insert(bytes.end(), class_payload.begin(), class_payload.end());

    WriteU32(bytes, 12, static_cast<std::uint32_t>(names.size()));
    WriteU32(bytes, 16, static_cast<std::uint32_t>(name_offset));
    WriteU32(bytes, 20, 1);
    WriteU32(bytes, 24, static_cast<std::uint32_t>(export_offset));
    WriteU32(bytes, 28, 2);
    WriteU32(bytes, 32, static_cast<std::uint32_t>(import_offset));

    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::uint32_t PackMeshVertex(std::int32_t x, std::int32_t y, std::int32_t z) {
    return (static_cast<std::uint32_t>(x) & 0x7ffu)
        | ((static_cast<std::uint32_t>(y) & 0x7ffu) << 11u)
        | ((static_cast<std::uint32_t>(z) & 0x3ffu) << 22u);
}

std::vector<std::uint8_t> MakeMeshPayload(std::int32_t serial_offset) {
    std::vector<std::uint8_t> payload;
    AppendCompactIndex(payload, 0);  // tagged-property terminator: None

    AppendVec3(payload, -8.0f, -8.0f, -8.0f);  // primitive bounds min
    AppendVec3(payload, 8.0f, 8.0f, 8.0f);     // primitive bounds max
    payload.push_back(1);                       // bounds valid
    AppendVec3(payload, 0.0f, 0.0f, 0.0f);     // sphere center
    AppendF32(payload, 12.0f);                  // sphere radius

    const std::size_t vertices_end_position = payload.size();
    AppendU32(payload, 0);
    AppendCompactIndex(payload, 3);
    AppendU32(payload, PackMeshVertex(1, 2, 3));
    AppendU32(payload, PackMeshVertex(5, 2, 3));
    AppendU32(payload, PackMeshVertex(1, 6, 3));
    WriteU32(payload, vertices_end_position,
             static_cast<std::uint32_t>(serial_offset + payload.size()));

    const std::size_t triangles_end_position = payload.size();
    AppendU32(payload, 0);
    AppendCompactIndex(payload, 1);
    AppendU16(payload, 0);
    AppendU16(payload, 1);
    AppendU16(payload, 2);
    payload.insert(payload.end(), {0u, 0u, 255u, 0u, 0u, 255u});
    AppendU32(payload, 0);  // triangle flags
    AppendU32(payload, 0);  // texture slot
    WriteU32(payload, triangles_end_position,
             static_cast<std::uint32_t>(serial_offset + payload.size()));

    AppendCompactIndex(payload, 0);  // animation sequences
    const std::size_t connects_end_position = payload.size();
    AppendU32(payload, 0);
    AppendCompactIndex(payload, 0);
    WriteU32(payload, connects_end_position,
             static_cast<std::uint32_t>(serial_offset + payload.size()));

    AppendVec3(payload, -8.0f, -8.0f, -8.0f);  // mesh bounds min
    AppendVec3(payload, 8.0f, 8.0f, 8.0f);     // mesh bounds max
    payload.push_back(1);
    AppendVec3(payload, 0.0f, 0.0f, 0.0f);
    AppendF32(payload, 12.0f);

    const std::size_t links_end_position = payload.size();
    AppendU32(payload, 0);
    AppendCompactIndex(payload, 0);
    WriteU32(payload, links_end_position,
             static_cast<std::uint32_t>(serial_offset + payload.size()));

    AppendCompactIndex(payload, 0);  // textures
    AppendCompactIndex(payload, 0);  // bounding boxes
    AppendCompactIndex(payload, 0);  // bounding spheres
    AppendU32(payload, 3);           // frame vertices
    AppendU32(payload, 1);           // animation frames
    AppendU32(payload, 0);           // mesh flags and mesh-and-lod flags
    AppendU32(payload, 0);
    AppendVec3(payload, 1.0f, 1.0f, 1.0f);  // mesh scale
    AppendVec3(payload, 0.0f, 0.0f, 0.0f);  // mesh origin
    AppendU32(payload, 0);           // rotation origin pitch
    AppendU32(payload, 0);           // rotation origin yaw
    AppendU32(payload, 0);           // rotation origin roll
    AppendU32(payload, 0);           // current polygon
    AppendU32(payload, 0);           // current vertex
    AppendCompactIndex(payload, 0);  // texture LOD array (version >= 66)
    return payload;
}

void WriteSyntheticMeshPackage(const std::filesystem::path& path) {
    std::vector<std::uint8_t> bytes(64, 0);
    WriteU32(bytes, 0, hp2::kUnrealPackageTag);
    WriteU16(bytes, 4, 79);

    const std::vector<std::string> names = {
        "None", "Core", "Class", "Mesh", "ChairMesh"
    };
    const std::size_t name_offset = bytes.size();
    for (const auto& name : names) {
        AppendName(bytes, name);
    }

    const std::size_t import_offset = bytes.size();
    AppendCompactIndex(bytes, 1);  // Core
    AppendCompactIndex(bytes, 2);  // Class
    AppendU32(bytes, 0);
    AppendCompactIndex(bytes, 3);  // Mesh

    const std::size_t export_offset = bytes.size();
    std::size_t table_size = 16;
    std::vector<std::uint8_t> export_table;
    std::vector<std::uint8_t> mesh_payload;
    for (int attempt = 0; attempt < 8; ++attempt) {
        const auto mesh_offset = static_cast<std::int32_t>(export_offset + table_size);
        mesh_payload = MakeMeshPayload(mesh_offset);
        export_table.clear();
        AppendExportRecord(export_table, -1, 4,
                           static_cast<std::int32_t>(mesh_payload.size()), mesh_offset);
        if (export_table.size() == table_size) {
            break;
        }
        table_size = export_table.size();
    }
    bytes.insert(bytes.end(), export_table.begin(), export_table.end());
    bytes.insert(bytes.end(), mesh_payload.begin(), mesh_payload.end());

    WriteU32(bytes, 12, static_cast<std::uint32_t>(names.size()));
    WriteU32(bytes, 16, static_cast<std::uint32_t>(name_offset));
    WriteU32(bytes, 20, 1);
    WriteU32(bytes, 24, static_cast<std::uint32_t>(export_offset));
    WriteU32(bytes, 28, 1);
    WriteU32(bytes, 32, static_cast<std::uint32_t>(import_offset));

    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::vector<std::uint8_t> MakeTexturePayload(std::int32_t serial_offset) {
    std::vector<std::uint8_t> payload;
    AppendCompactIndex(payload, 4);  // Palette property
    payload.push_back(0x25u);        // Object, four-byte nominal property size
    AppendCompactIndex(payload, 2);  // second export: TestPalette
    AppendCompactIndex(payload, 0);  // tagged-property terminator: None

    AppendCompactIndex(payload, 1);  // mip count
    const std::size_t lazy_end_position = payload.size();
    AppendU32(payload, 0);           // absolute end of lazy pixel array
    AppendCompactIndex(payload, 4);  // pixel-index count
    payload.insert(payload.end(), {1u, 2u, 3u, 4u});
    WriteU32(payload, lazy_end_position,
             static_cast<std::uint32_t>(serial_offset + payload.size()));
    AppendU32(payload, 2);           // USize
    AppendU32(payload, 2);           // VSize
    payload.push_back(1);            // UBits
    payload.push_back(1);            // VBits
    return payload;
}

std::vector<std::uint8_t> MakePalettePayload() {
    std::vector<std::uint8_t> payload;
    AppendCompactIndex(payload, 0);    // tagged-property terminator: None
    AppendCompactIndex(payload, 256);  // palette colors
    for (std::int32_t index = 0; index < 256; ++index) {
        const std::uint8_t red = index == 1 || index == 4 ? 255u : 0u;
        const std::uint8_t green = index == 2 || index == 4 ? 255u : 0u;
        const std::uint8_t blue = index == 3 || index == 4 ? 255u : 0u;
        payload.insert(payload.end(), {red, green, blue, 0u});
    }
    return payload;
}

void WriteSyntheticTexturePackage(const std::filesystem::path& path) {
    std::vector<std::uint8_t> bytes(64, 0);
    WriteU32(bytes, 0, hp2::kUnrealPackageTag);
    WriteU16(bytes, 4, 79);

    const std::vector<std::string> names = {
        "None", "Core", "Class", "Texture", "Palette", "TestTexture", "TestPalette"
    };
    const std::size_t name_offset = bytes.size();
    for (const auto& name : names) {
        AppendName(bytes, name);
    }

    const std::size_t import_offset = bytes.size();
    AppendCompactIndex(bytes, 1);  // Core
    AppendCompactIndex(bytes, 2);  // Class
    AppendU32(bytes, 0);
    AppendCompactIndex(bytes, 3);  // Texture
    AppendCompactIndex(bytes, 1);  // Core
    AppendCompactIndex(bytes, 2);  // Class
    AppendU32(bytes, 0);
    AppendCompactIndex(bytes, 4);  // Palette

    const std::size_t export_offset = bytes.size();
    const std::vector<std::uint8_t> palette_payload = MakePalettePayload();
    std::size_t table_size = 32;
    std::vector<std::uint8_t> export_table;
    std::vector<std::uint8_t> texture_payload;
    for (int attempt = 0; attempt < 8; ++attempt) {
        const std::int32_t texture_offset = static_cast<std::int32_t>(export_offset + table_size);
        texture_payload = MakeTexturePayload(texture_offset);
        const std::int32_t palette_offset = texture_offset
            + static_cast<std::int32_t>(texture_payload.size());
        export_table.clear();
        AppendExportRecord(export_table, -1, 5,
                           static_cast<std::int32_t>(texture_payload.size()), texture_offset);
        AppendExportRecord(export_table, -2, 6,
                           static_cast<std::int32_t>(palette_payload.size()), palette_offset);
        if (export_table.size() == table_size) {
            break;
        }
        table_size = export_table.size();
    }
    bytes.insert(bytes.end(), export_table.begin(), export_table.end());
    bytes.insert(bytes.end(), texture_payload.begin(), texture_payload.end());
    bytes.insert(bytes.end(), palette_payload.begin(), palette_payload.end());

    WriteU32(bytes, 12, static_cast<std::uint32_t>(names.size()));
    WriteU32(bytes, 16, static_cast<std::uint32_t>(name_offset));
    WriteU32(bytes, 20, 2);
    WriteU32(bytes, 24, static_cast<std::uint32_t>(export_offset));
    WriteU32(bytes, 28, 2);
    WriteU32(bytes, 32, static_cast<std::uint32_t>(import_offset));

    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}

}  // namespace

int main() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() / ("hp2-portcore-test-" + std::to_string(nonce));
    std::filesystem::create_directories(root / "Maps");
    std::filesystem::create_directories(root / "System");
    std::filesystem::create_directories(root / "Textures");
    WriteSyntheticTexturePackage(root / "Textures" / "SyntheticTex.utx");
    WriteSyntheticMeshPackage(root / "System" / "MeshPack.u");
    WriteSyntheticClassPackage(root / "System" / "ActorClasses.u");
    WriteSyntheticActorPackage(root / "Maps" / "SyntheticActors.unr");
    WriteSyntheticClassPackage(root / "System" / "ActorClassesExtreme.u", 200.0f);
    WriteSyntheticActorPackage(
        root / "Maps" / "SyntheticActorsExtreme.unr", "ActorClassesExtreme"
    );
    WriteSyntheticActorPackage(
        root / "Maps" / "SyntheticActorsDirectExtreme.unr",
        "ActorClassesExtreme", true, 200.0f
    );

    std::vector<std::uint8_t> bytes(64, 0);
    WriteU32(bytes, 0, hp2::kUnrealPackageTag);
    WriteU16(bytes, 4, 79);
    WriteU16(bytes, 6, 0);

    const std::vector<std::string> names = {
        "None", "Core", "Class", "Model", "MyLevel", "Level", "Model314",
        "Package", "Texture", "SyntheticTex", "TestTexture"
    };
    const std::size_t name_offset = bytes.size();
    for (const auto& name : names) {
        AppendName(bytes, name);
    }

    const std::size_t import_offset = bytes.size();
    AppendCompactIndex(bytes, 1);  // Core
    AppendCompactIndex(bytes, 2);  // Class
    AppendU32(bytes, 0);           // outer
    AppendCompactIndex(bytes, 3);  // Model
    AppendCompactIndex(bytes, 1);  // Core
    AppendCompactIndex(bytes, 7);  // Package
    AppendU32(bytes, 0);           // outer
    AppendCompactIndex(bytes, 9);  // SyntheticTex
    AppendCompactIndex(bytes, 1);  // Core
    AppendCompactIndex(bytes, 8);  // Texture
    AppendU32(bytes, 0xfffffffeu); // outer: second import (SyntheticTex)
    AppendCompactIndex(bytes, 10); // TestTexture

    std::vector<std::uint8_t> model_payload;
    AppendCompactIndex(model_payload, 0);  // tagged-property terminator: None

    AppendVec3(model_payload, -1.0f, -1.0f, -1.0f);  // primitive bounds min
    AppendVec3(model_payload, 1.0f, 1.0f, 1.0f);     // primitive bounds max
    model_payload.push_back(1);                       // bounds valid
    AppendVec3(model_payload, 0.0f, 0.0f, 0.0f);     // sphere center
    AppendF32(model_payload, 1.75f);                  // sphere radius

    AppendCompactIndex(model_payload, 3);             // vectors
    AppendVec3(model_payload, 0.0f, 0.0f, 1.0f);
    AppendVec3(model_payload, 1.0f, 0.0f, 0.0f);
    AppendVec3(model_payload, 0.0f, 1.0f, 0.0f);

    AppendCompactIndex(model_payload, 4);             // points
    AppendVec3(model_payload, -1.0f, -1.0f, 0.0f);
    AppendVec3(model_payload, 1.0f, -1.0f, 0.0f);
    AppendVec3(model_payload, 1.0f, 1.0f, 0.0f);
    AppendVec3(model_payload, -1.0f, 1.0f, 0.0f);

    AppendCompactIndex(model_payload, 1);             // nodes
    AppendF32(model_payload, 0.0f);                   // plane X
    AppendF32(model_payload, 0.0f);                   // plane Y
    AppendF32(model_payload, 1.0f);                   // plane Z
    AppendF32(model_payload, 0.0f);                   // plane W
    AppendU64(model_payload, 0);                      // zone mask
    model_payload.push_back(0);                       // node flags
    AppendCompactIndex(model_payload, 0);             // vertex pool
    AppendCompactIndex(model_payload, 0);             // surface
    AppendCompactIndex(model_payload, -1);            // back
    AppendCompactIndex(model_payload, -1);            // front
    AppendCompactIndex(model_payload, -1);            // coplanar
    AppendCompactIndex(model_payload, -1);            // collision bound
    AppendCompactIndex(model_payload, -1);            // render bound
    AppendCompactIndex(model_payload, 0);             // back zone
    AppendCompactIndex(model_payload, 0);             // front zone
    model_payload.push_back(4);                       // polygon vertex count
    AppendU32(model_payload, 0xffffffffu);             // back leaf
    AppendU32(model_payload, 0xffffffffu);             // front leaf

    AppendCompactIndex(model_payload, 1);             // surfaces
    AppendCompactIndex(model_payload, -3);            // TestTexture import
    AppendU32(model_payload, 0);                      // poly flags
    AppendCompactIndex(model_payload, 0);             // base point
    AppendCompactIndex(model_payload, 0);             // normal vector
    AppendCompactIndex(model_payload, 1);             // texture U vector
    AppendCompactIndex(model_payload, 2);             // texture V vector
    AppendCompactIndex(model_payload, 0);             // first light map
    AppendCompactIndex(model_payload, -1);            // source brush polygon
    AppendU16(model_payload, 1);                      // pan U
    AppendU16(model_payload, 0xffffu);                // pan V (-1)
    AppendCompactIndex(model_payload, 0);             // source actor

    AppendCompactIndex(model_payload, 4);             // BSP vertex pool
    for (std::int32_t point_index = 0; point_index < 4; ++point_index) {
        AppendCompactIndex(model_payload, point_index);
        AppendCompactIndex(model_payload, -1);
    }
    AppendU32(model_payload, 0);                      // shared sides
    AppendU32(model_payload, 0);                      // zones
    AppendCompactIndex(model_payload, 0);             // Polys object ref

    AppendCompactIndex(model_payload, 1);             // light-map indices
    AppendU32(model_payload, 0);                      // LightBits offset
    AppendVec3(model_payload, 0.0f, 0.0f, 0.0f);     // light-map pan
    AppendCompactIndex(model_payload, 4);             // U clamp
    AppendCompactIndex(model_payload, 4);             // V clamp
    AppendF32(model_payload, 1.0f);                   // U scale
    AppendF32(model_payload, 1.0f);                   // V scale
    AppendU32(model_payload, 0);                      // first light actor

    AppendCompactIndex(model_payload, 4);             // LightBits bytes
    model_payload.insert(model_payload.end(), {0x0fu, 0x0fu, 0x05u, 0x0au});
    AppendCompactIndex(model_payload, 0);             // model bounds
    AppendCompactIndex(model_payload, 0);             // leaf hulls
    AppendCompactIndex(model_payload, 0);             // convex leaves
    AppendCompactIndex(model_payload, 2);             // light actor references
    AppendCompactIndex(model_payload, 1);             // synthetic light object
    AppendCompactIndex(model_payload, 0);             // light-list terminator

    const std::size_t export_offset = bytes.size();
    AppendCompactIndex(bytes, -1); // class = first import (Model)
    AppendCompactIndex(bytes, 0);  // superclass
    AppendU32(bytes, 0);           // outer
    AppendCompactIndex(bytes, 6);  // Model314
    AppendU32(bytes, 0x00000001u); // flags
    AppendCompactIndex(bytes, static_cast<std::int32_t>(model_payload.size()));
    const std::size_t serial_offset_base = bytes.size();
    std::size_t serial_offset = serial_offset_base + 1;
    std::vector<std::uint8_t> encoded_serial_offset;
    for (int attempt = 0; attempt < 3; ++attempt) {
        encoded_serial_offset.clear();
        AppendCompactIndex(encoded_serial_offset, static_cast<std::int32_t>(serial_offset));
        const std::size_t adjusted_offset = serial_offset_base + encoded_serial_offset.size();
        if (adjusted_offset == serial_offset) {
            break;
        }
        serial_offset = adjusted_offset;
    }
    bytes.insert(bytes.end(), encoded_serial_offset.begin(), encoded_serial_offset.end());
    bytes.insert(bytes.end(), model_payload.begin(), model_payload.end());

    WriteU32(bytes, 12, static_cast<std::uint32_t>(names.size()));
    WriteU32(bytes, 16, static_cast<std::uint32_t>(name_offset));
    WriteU32(bytes, 20, 1);
    WriteU32(bytes, 24, static_cast<std::uint32_t>(export_offset));
    WriteU32(bytes, 28, 3);
    WriteU32(bytes, 32, static_cast<std::uint32_t>(import_offset));

    const auto valid_path = root / "Maps" / "Synthetic.unr";
    {
        std::ofstream output(valid_path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    bool ok = true;
    const auto summary = hp2::ProbePackage(valid_path);
    ok &= Expect(summary.valid, "synthetic package should be valid");
    ok &= Expect(summary.file_version == 79, "file version should be decoded as little-endian");
    ok &= Expect(summary.name_count == 11 && summary.import_count == 3 && summary.export_count == 1,
                 "table counts should be decoded");

    const auto package = hp2::LoadPackageIndex(valid_path);
    ok &= Expect(package.valid, "synthetic package index should parse");
    ok &= Expect(package.names.size() == 11 && package.names[4].value == "MyLevel",
                 "name table should parse compact strings");
    ok &= Expect(package.imports.size() == 3 && package.imports[0].object_name == "Model",
                 "import table should resolve names");
    ok &= Expect(package.exports.size() == 1 && package.exports[0].class_name == "Model",
                 "export class reference should resolve through imports");
    ok &= Expect(package.exports.size() == 1 && hp2::IsGeometryCandidate(package.exports[0]),
                 "model export should be selected as a geometry candidate");

    ok &= Expect(hp2::FindLargestModelExport(package) == 0,
                 "largest Model export should be selected");
    const auto model = hp2::LoadModelGeometry(package, 0);
    ok &= Expect(model.valid, "synthetic Model payload should decode");
    ok &= Expect(model.vectors.size() == 3 && model.points.size() == 4,
                 "Model vectors and points should decode");
    ok &= Expect(model.nodes.size() == 1 && model.surfaces.size() == 1 && model.vertices.size() == 4,
                 "Model BSP arrays should decode");
    ok &= Expect(model.triangles.size() == 2,
                 "four-vertex BSP face should triangulate into two triangles");
    ok &= Expect(model.bounds_min.x == -1.0f && model.bounds_max.y == 1.0f,
                 "triangulated geometry bounds should be computed");
    ok &= Expect(model.surfaces[0].pan_u == 1 && model.surfaces[0].pan_v == -1,
                 "signed BSP texture panning should decode");
    ok &= Expect(model.surfaces[0].light_map_index == 0 && model.light_maps.size() == 1
                     && model.light_bits.size() == 4 && model.light_actor_references.size() == 2,
                 "UE1 light-map indices, bits and actor references should decode");

    hp2::TextureCoordinate coordinate;
    ok &= Expect(hp2::ComputeSurfaceTextureCoordinate(model, 0, 2, 2, 2, coordinate),
                 "BSP texture coordinate should compute");
    ok &= Expect(std::abs(coordinate.u - 1.5f) < 0.0001f
                     && std::abs(coordinate.v - 0.5f) < 0.0001f,
                 "BSP texture coordinate should include base vectors and signed pan");

    const auto light_map_atlas = hp2::BuildVisibilityLightMapAtlas(model, 256);
    ok &= Expect(light_map_atlas.valid && light_map_atlas.referenced_light_maps == 1
                     && light_map_atlas.shadow_mask_count == 1
                     && light_map_atlas.lit_triangles == 2,
                 "UE1 LightBits should build a bounded visibility-lightmap atlas");
    hp2::TextureCoordinate light_coordinate;
    ok &= Expect(hp2::ComputeSurfaceLightMapCoordinate(
                     model, light_map_atlas, 0, 2, light_coordinate
                 ) && light_coordinate.u > 0.0f && light_coordinate.u < 1.0f
                     && light_coordinate.v > 0.0f && light_coordinate.v < 1.0f,
                 "BSP light-map coordinates should map into the generated atlas");

    const auto texture_package = hp2::LoadPackageIndex(root / "Textures" / "SyntheticTex.utx");
    ok &= Expect(texture_package.valid && texture_package.exports.size() == 2,
                 "synthetic texture package should parse");
    const auto direct_texture = hp2::LoadTextureExport(texture_package, 0);
    ok &= Expect(direct_texture.valid && direct_texture.width == 2 && direct_texture.height == 2,
                 "P8 texture mip and palette should decode");
    ok &= Expect(direct_texture.rgba_pixels.size() == 16
                     && direct_texture.rgba_pixels[0] == 255
                     && direct_texture.rgba_pixels[1] == 0
                     && direct_texture.rgba_pixels[4] == 0
                     && direct_texture.rgba_pixels[5] == 255,
                 "palette indices should expand into RGBA pixels");

    const auto surface_texture = hp2::LoadFirstSurfaceTexture(root, package, model);
    ok &= Expect(surface_texture.valid && surface_texture.map_material_index == -3
                     && surface_texture.triangle_count == 2
                     && surface_texture.object_name == "TestTexture",
                 "dominant BSP material should resolve to its external UTX texture");
    const auto surface_textures = hp2::LoadSurfaceTextures(root, package, model);
    ok &= Expect(surface_textures.valid && surface_textures.textures.size() == 1
                     && surface_textures.material_candidates == 1
                     && surface_textures.textured_triangles == 2,
                 "all decodable BSP materials should be collected for batched rendering");

    const auto actor_package = hp2::LoadPackageIndex(root / "Maps" / "SyntheticActors.unr");
    ok &= Expect(actor_package.valid && actor_package.exports.size() == 2,
                 "synthetic Level and actor exports should parse");
    const auto actor_census = hp2::LoadLevelActorCensus(actor_package);
    ok &= Expect(actor_census.valid && actor_census.actor_reference_count == 1
                     && actor_census.non_null_actor_references == 1
                     && actor_census.parsed_actor_count == 1,
                 "ULevel actor references and StateFrame properties should decode");
    ok &= Expect(actor_census.actors.size() == 1
                     && actor_census.actors[0].class_name == "Decoration"
                     && actor_census.actors[0].class_reference == -3
                     && actor_census.actors[0].has_location
                     && actor_census.actors[0].location.x == 10.0f
                     && actor_census.actors[0].has_rotation
                     && actor_census.actors[0].rotation.yaw == 2048,
                 "actor class and transform properties should decode");
    ok &= Expect(actor_census.actors[0].mesh_reference == 0
                     && !actor_census.actors[0].has_draw_scale
                     && actor_census.actors[0].has_draw_scale_3d
                     && actor_census.actors[0].draw_scale_3d.z == 3.0f
                     && actor_census.actors[0].has_pre_pivot
                     && !actor_census.actors[0].hidden,
                 "actor overrides should remain distinct from inherited class defaults");
    const auto class_package = hp2::LoadPackageIndex(root / "System" / "ActorClasses.u");
    const auto class_defaults = hp2::ScanClassDefaultProperties(class_package, 0);
    float inherited_scale = 0.0f;
    ok &= Expect(class_package.valid && class_defaults.valid
                     && hp2::DecodePropertyObjectReference(
                         hp2::FindObjectProperty(class_defaults, "Mesh")
                     ) == -2
                     && hp2::DecodePropertyFloat(
                         hp2::FindObjectProperty(class_defaults, "DrawScale"), inherited_scale
                     )
                     && std::abs(inherited_scale - 1.25f) < 0.0001f,
                 "bounded UClass tail scan should recover typed default properties");
    const auto mesh_package = hp2::LoadPackageIndex(root / "System" / "MeshPack.u");
    const auto decoded_mesh = hp2::LoadVertexMeshExport(mesh_package, 0);
    ok &= Expect(decoded_mesh.valid && decoded_mesh.vertices.size() == 3
                     && decoded_mesh.triangles.size() == 1
                     && decoded_mesh.frame_vertices == 3
                     && decoded_mesh.animation_frames == 1,
                 "UE1 Mesh reference-pose geometry should decode");
    const auto actor_mesh_scene = hp2::LoadDirectActorMeshes(root, actor_package, actor_census);
    ok &= Expect(actor_mesh_scene.valid && actor_mesh_scene.candidate_instances == 1
                     && actor_mesh_scene.direct_mesh_candidates == 0
                     && actor_mesh_scene.inherited_mesh_candidates == 1
                     && actor_mesh_scene.class_exports_scanned == 1
                     && actor_mesh_scene.class_default_streams_found == 1
                     && actor_mesh_scene.decoded_mesh_assets == 1
                     && actor_mesh_scene.decoded_mesh_instances == 1
                     && actor_mesh_scene.decoded_inherited_mesh_instances == 1
                     && actor_mesh_scene.source_triangles == 1
                     && actor_mesh_scene.instances.size() == 1
                     && actor_mesh_scene.instances[0].inherited_mesh
                     && actor_mesh_scene.instances[0].inherited_draw_scale
                     && actor_mesh_scene.instances[0].source_draw_scale == 1.25f
                     && !actor_mesh_scene.instances[0].rejected_inherited_draw_scale
                     && actor_mesh_scene.instances[0].emitted_triangles == 1
                     && actor_mesh_scene.instances[0].bounds_valid
                     && actor_mesh_scene.triangles.size() == 1
                     && actor_mesh_scene.bounds_valid
                     && actor_mesh_scene.bounds_max.x > actor_mesh_scene.bounds_min.x,
                 "inherited class Mesh references should resolve and receive the actor transform");
    ok &= Expect(std::isfinite(actor_mesh_scene.triangles[0].points[0].x)
                     && actor_mesh_scene.assets[0].object_name == "ChairMesh"
                     && actor_mesh_scene.assets[0].vertex_bounds_valid,
                 "placed actor triangles and mesh asset summaries should remain bounded");

    const auto extreme_actor_package = hp2::LoadPackageIndex(
        root / "Maps" / "SyntheticActorsExtreme.unr"
    );
    const auto extreme_actor_census = hp2::LoadLevelActorCensus(extreme_actor_package);
    const auto extreme_actor_mesh_scene = hp2::LoadDirectActorMeshes(
        root, extreme_actor_package, extreme_actor_census
    );
    ok &= Expect(extreme_actor_mesh_scene.valid
                     && extreme_actor_mesh_scene.rejected_inherited_draw_scales == 1
                     && extreme_actor_mesh_scene.instances.size() == 1
                     && extreme_actor_mesh_scene.instances[0].inherited_draw_scale
                     && extreme_actor_mesh_scene.instances[0].source_draw_scale == 200.0f
                     && extreme_actor_mesh_scene.instances[0].rejected_inherited_draw_scale
                     && !extreme_actor_mesh_scene.instances[0].has_draw_scale
                     && extreme_actor_mesh_scene.instances[0].draw_scale == 1.0f
                     && extreme_actor_mesh_scene.instances[0].bounds_max.x
                         - extreme_actor_mesh_scene.instances[0].bounds_min.x < 100.0f,
                 "extreme inherited DrawScale should fall back without altering direct values");

    const auto direct_extreme_actor_package = hp2::LoadPackageIndex(
        root / "Maps" / "SyntheticActorsDirectExtreme.unr"
    );
    const auto direct_extreme_actor_census = hp2::LoadLevelActorCensus(
        direct_extreme_actor_package
    );
    const auto direct_extreme_actor_mesh_scene = hp2::LoadDirectActorMeshes(
        root, direct_extreme_actor_package, direct_extreme_actor_census
    );
    ok &= Expect(direct_extreme_actor_mesh_scene.valid
                     && direct_extreme_actor_mesh_scene.rejected_inherited_draw_scales == 0
                     && direct_extreme_actor_mesh_scene.instances.size() == 1
                     && !direct_extreme_actor_mesh_scene.instances[0].inherited_draw_scale
                     && !direct_extreme_actor_mesh_scene.instances[0].rejected_inherited_draw_scale
                     && direct_extreme_actor_mesh_scene.instances[0].has_draw_scale
                     && direct_extreme_actor_mesh_scene.instances[0].draw_scale == 200.0f,
                 "direct actor DrawScale overrides should remain authoritative");

    hp2::Runtime runtime;
    runtime.Initialize(root);
    ok &= Expect(runtime.status() == hp2::BootStatus::WaitingForController,
                 "runtime must require a controller before advancing");
    runtime.SetControllerPresent(true);
    ok &= Expect(runtime.status() == hp2::BootStatus::PackageProbeReady,
                 "runtime should advance after valid package and controller detection");

    hp2::AnalogState analog;
    analog.left_x = 4.0f;
    analog.right_trigger = -2.0f;
    runtime.SetAnalog(analog);
    ok &= Expect(runtime.analog().left_x == 1.0f, "axis should be clamped");
    ok &= Expect(runtime.analog().right_trigger == 0.0f, "trigger should be clamped");

    bytes[0] = 0;
    const auto invalid_path = root / "Maps" / "Broken.unr";
    {
        std::ofstream output(invalid_path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    ok &= Expect(!hp2::ProbePackage(invalid_path).valid, "bad package tag should fail");

    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);
    if (!ok) {
        return 1;
    }
    std::cout << "portcore_tests: PASS\n";
    return 0;
}
