#include "hp2/ue_package.h"
#include "hp2/ue_skeletal.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void AppendU16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
}

void AppendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        bytes.push_back(static_cast<std::uint8_t>((value >> (index * 8u)) & 0xffu));
    }
}

void WriteU16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    bytes.at(offset) = static_cast<std::uint8_t>(value & 0xffu);
    bytes.at(offset + 1) = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
}

void WriteU32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        bytes.at(offset + index) = static_cast<std::uint8_t>((value >> (index * 8u)) & 0xffu);
    }
}

void AppendF32(std::vector<std::uint8_t>& bytes, float value) {
    std::uint32_t raw = 0;
    static_assert(sizeof(raw) == sizeof(value));
    std::memcpy(&raw, &value, sizeof(raw));
    AppendU32(bytes, raw);
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
    std::int32_t serial_offset
) {
    AppendCompactIndex(bytes, class_index);
    AppendCompactIndex(bytes, 0);
    AppendU32(bytes, 0);
    AppendCompactIndex(bytes, object_name_index);
    AppendU32(bytes, 1);
    AppendCompactIndex(bytes, serial_size);
    AppendCompactIndex(bytes, serial_offset);
}

void AppendPrimitiveBounds(std::vector<std::uint8_t>& payload) {
    AppendVec3(payload, -8.0f, -8.0f, -8.0f);
    AppendVec3(payload, 8.0f, 8.0f, 8.0f);
    payload.push_back(1);
    AppendVec3(payload, 0.0f, 0.0f, 0.0f);
    AppendF32(payload, 12.0f);
}

std::vector<std::uint8_t> MakeSkeletalPayload(std::int32_t serial_offset) {
    std::vector<std::uint8_t> payload;
    AppendCompactIndex(payload, 0);
    AppendPrimitiveBounds(payload);

    const std::size_t verts_end_offset = payload.size();
    AppendU32(payload, 0);
    AppendCompactIndex(payload, 3);
    AppendU32(payload, 0);
    AppendU32(payload, 0);
    AppendU32(payload, 0);
    WriteU32(payload, verts_end_offset,
             static_cast<std::uint32_t>(serial_offset + payload.size()));

    const std::size_t tris_end_offset = payload.size();
    AppendU32(payload, 0);
    AppendCompactIndex(payload, 0);
    WriteU32(payload, tris_end_offset,
             static_cast<std::uint32_t>(serial_offset + payload.size()));

    AppendCompactIndex(payload, 1);
    AppendCompactIndex(payload, 5);
    AppendCompactIndex(payload, 0);
    AppendU32(payload, 0);
    AppendU32(payload, 4);
    AppendCompactIndex(payload, 0);
    AppendF32(payload, 30.0f);

    const std::size_t connects_end_offset = payload.size();
    AppendU32(payload, 0);
    AppendCompactIndex(payload, 0);
    WriteU32(payload, connects_end_offset,
             static_cast<std::uint32_t>(serial_offset + payload.size()));

    AppendPrimitiveBounds(payload);

    const std::size_t links_end_offset = payload.size();
    AppendU32(payload, 0);
    AppendCompactIndex(payload, 0);
    WriteU32(payload, links_end_offset,
             static_cast<std::uint32_t>(serial_offset + payload.size()));

    AppendCompactIndex(payload, 0);
    AppendCompactIndex(payload, 0);
    AppendCompactIndex(payload, 0);
    AppendU32(payload, 3);
    AppendU32(payload, 4);
    AppendU32(payload, 0);
    AppendU32(payload, 0);
    AppendVec3(payload, 1.0f, 1.0f, 1.0f);
    AppendVec3(payload, 0.0f, 0.0f, 0.0f);
    AppendU32(payload, 0);
    AppendU32(payload, 0);
    AppendU32(payload, 0);
    AppendU32(payload, 0);
    AppendU32(payload, 0);
    AppendCompactIndex(payload, 0);

    AppendCompactIndex(payload, 0);
    AppendCompactIndex(payload, 0);
    AppendCompactIndex(payload, 0);
    AppendCompactIndex(payload, 0);
    AppendCompactIndex(payload, 0);
    AppendCompactIndex(payload, 0);
    AppendCompactIndex(payload, 0);
    AppendU32(payload, 0);
    AppendU32(payload, 0);
    AppendF32(payload, 0.0f);
    AppendF32(payload, 0.0f);
    AppendF32(payload, 0.0f);
    AppendU32(payload, 0);
    AppendF32(payload, 0.0f);
    AppendF32(payload, 0.0f);
    AppendCompactIndex(payload, 0);
    AppendU32(payload, 0);

    AppendCompactIndex(payload, 0);
    AppendCompactIndex(payload, 3);
    AppendVec3(payload, 0.0f, 0.0f, 0.0f);
    AppendVec3(payload, 1.0f, 0.0f, 0.0f);
    AppendVec3(payload, 0.0f, 1.0f, 0.0f);

    AppendCompactIndex(payload, 2);
    AppendCompactIndex(payload, 6);
    AppendU32(payload, 0);
    AppendF32(payload, 0.0f);
    AppendF32(payload, 0.0f);
    AppendF32(payload, 0.0f);
    AppendF32(payload, 1.0f);
    AppendVec3(payload, 0.0f, 0.0f, 0.0f);
    AppendF32(payload, 1.0f);
    AppendVec3(payload, 1.0f, 1.0f, 1.0f);
    AppendU32(payload, 1);
    AppendU32(payload, 0);

    AppendCompactIndex(payload, 7);
    AppendU32(payload, 0);
    AppendF32(payload, 0.0f);
    AppendF32(payload, 0.0f);
    AppendF32(payload, 0.0f);
    AppendF32(payload, 1.0f);
    AppendVec3(payload, 0.0f, 0.0f, 1.0f);
    AppendF32(payload, 1.0f);
    AppendVec3(payload, 1.0f, 1.0f, 1.0f);
    AppendU32(payload, 0);
    AppendU32(payload, 0);

    AppendCompactIndex(payload, 3);
    AppendU32(payload, 0);
    AppendU32(payload, 0);
    AppendU32(payload, 1);
    AppendU32(payload, 1);
    AppendU32(payload, 2);
    AppendU32(payload, 2);

    AppendCompactIndex(payload, 3);
    AppendF32(payload, 1.0f);
    AppendF32(payload, 0.75f);
    AppendF32(payload, 0.25f);

    AppendCompactIndex(payload, 3);
    AppendVec3(payload, 0.0f, 0.0f, 0.0f);
    AppendVec3(payload, 1.0f, 0.0f, 0.0f);
    AppendVec3(payload, 0.0f, 1.0f, 0.0f);
    AppendU32(payload, 0);
    AppendCompactIndex(payload, 0);
    AppendU32(payload, 0);
    payload.insert(payload.end(), 48u, 0u);
    return payload;
}

void WriteSyntheticSkeletalPackage(const std::filesystem::path& path) {
    std::vector<std::uint8_t> bytes(64, 0);
    WriteU32(bytes, 0, hp2::kUnrealPackageTag);
    WriteU16(bytes, 4, 79);

    const std::vector<std::string> names = {
        "None", "Core", "Class", "SkeletalMesh", "TestMesh", "Idle", "Root", "Hand"
    };
    const std::size_t name_offset = bytes.size();
    for (const auto& name : names) {
        AppendName(bytes, name);
    }

    const std::size_t import_offset = bytes.size();
    AppendCompactIndex(bytes, 1);
    AppendCompactIndex(bytes, 2);
    AppendU32(bytes, 0);
    AppendCompactIndex(bytes, 3);

    const std::size_t export_offset = bytes.size();
    std::size_t table_size = 16;
    std::vector<std::uint8_t> export_table;
    std::vector<std::uint8_t> payload;
    for (int attempt = 0; attempt < 8; ++attempt) {
        const auto serial_offset = static_cast<std::int32_t>(export_offset + table_size);
        payload = MakeSkeletalPayload(serial_offset);
        export_table.clear();
        AppendExportRecord(export_table, -1, 4,
                           static_cast<std::int32_t>(payload.size()), serial_offset);
        if (export_table.size() == table_size) {
            break;
        }
        table_size = export_table.size();
    }
    bytes.insert(bytes.end(), export_table.begin(), export_table.end());
    bytes.insert(bytes.end(), payload.begin(), payload.end());

    WriteU32(bytes, 12, static_cast<std::uint32_t>(names.size()));
    WriteU32(bytes, 16, static_cast<std::uint32_t>(name_offset));
    WriteU32(bytes, 20, 1);
    WriteU32(bytes, 24, static_cast<std::uint32_t>(export_offset));
    WriteU32(bytes, 28, 1);
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
    const std::filesystem::path root = std::filesystem::temp_directory_path()
        / "hp2-skeletal-probe-test";
    std::error_code error_code;
    std::filesystem::remove_all(root, error_code);
    std::filesystem::create_directories(root, error_code);
    const std::filesystem::path package_path = root / "Synthetic.u";
    WriteSyntheticSkeletalPackage(package_path);

    const hp2::PackageIndex package = hp2::LoadPackageIndex(package_path);
    bool ok = true;
    ok &= Expect(package.valid, "synthetic package index should be valid");
    ok &= Expect(package.exports.size() == 1, "synthetic package should have one export");

    const hp2::SkeletalMeshSkinningData data = hp2::LoadSkeletalMeshSkinningExport(package, 0);
    ok &= Expect(data.valid, "synthetic skeletal stream should decode");
    ok &= Expect(data.sequences.size() == 1, "one animation sequence should decode");
    ok &= Expect(data.sequences[0].name == "Idle", "animation name should resolve");
    ok &= Expect(data.sequences[0].frame_count == 4, "animation frame count should decode");
    ok &= Expect(data.reference_points.size() == 3, "three reference points should decode");
    ok &= Expect(data.bones.size() == 2, "two reference bones should decode");
    ok &= Expect(data.bones[0].name == "Root", "root bone name should resolve");
    ok &= Expect(data.bones[1].parent_index == 0, "child parent should decode");
    ok &= Expect(data.root_like_bones == 1, "one root-like bone should be detected");
    ok &= Expect(data.invalid_parent_bones == 0, "bone parents should be valid");
    ok &= Expect(data.weight_indices.size() == 3, "three raw weight-index records should decode");
    ok &= Expect(data.weight_words.size() == 3, "three weight words should decode");
    ok &= Expect(data.finite_weight_words == 3, "all synthetic weight words should be finite");
    ok &= Expect(data.unit_interval_weight_words == 3, "all synthetic weights should be normalized");
    ok &= Expect(data.local_points.size() == 3, "three local points should decode");
    ok &= Expect(data.remaining_bytes == 0, "synthetic skeletal stream should be fully consumed");

    const hp2::SkeletalMeshSkinningData named = hp2::LoadNamedSkeletalMeshSkinning(
        root, "Synthetic", "TestMesh"
    );
    ok &= Expect(named.valid, "named skeletal lookup should find the synthetic mesh");

    std::filesystem::remove_all(root, error_code);
    if (!ok) {
        return 1;
    }
    std::cout << "skeletal probe tests passed\n";
    return 0;
}
