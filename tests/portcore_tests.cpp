#include "hp2/runtime.h"
#include "hp2/ue_package.h"

#include <chrono>
#include <cstdint>
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

    std::vector<std::uint8_t> bytes(64, 0);
    WriteU32(bytes, 0, hp2::kUnrealPackageTag);
    WriteU16(bytes, 4, 79);
    WriteU16(bytes, 6, 0);

    const std::vector<std::string> names = {"None", "Core", "Class", "Model", "MyLevel", "Level"};
    const std::size_t name_offset = bytes.size();
    for (const auto& name : names) {
        AppendName(bytes, name);
    }

    const std::size_t import_offset = bytes.size();
    AppendCompactIndex(bytes, 1);  // Core
    AppendCompactIndex(bytes, 2);  // Class
    AppendU32(bytes, 0);           // outer
    AppendCompactIndex(bytes, 3);  // Model

    const std::size_t export_offset = bytes.size();
    AppendCompactIndex(bytes, -1); // class = first import (Model)
    AppendCompactIndex(bytes, 0);  // superclass
    AppendU32(bytes, 0);           // outer
    AppendCompactIndex(bytes, 4);  // MyLevel
    AppendU32(bytes, 0x00000001u); // flags
    AppendCompactIndex(bytes, 4);  // serial size
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
    bytes.insert(bytes.end(), {1, 2, 3, 4});

    WriteU32(bytes, 12, static_cast<std::uint32_t>(names.size()));
    WriteU32(bytes, 16, static_cast<std::uint32_t>(name_offset));
    WriteU32(bytes, 20, 1);
    WriteU32(bytes, 24, static_cast<std::uint32_t>(export_offset));
    WriteU32(bytes, 28, 1);
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
    ok &= Expect(summary.name_count == 6 && summary.import_count == 1 && summary.export_count == 1,
                 "table counts should be decoded");

    const auto package = hp2::LoadPackageIndex(valid_path);
    ok &= Expect(package.valid, "synthetic package index should parse");
    ok &= Expect(package.names.size() == 6 && package.names[4].value == "MyLevel",
                 "name table should parse compact strings");
    ok &= Expect(package.imports.size() == 1 && package.imports[0].object_name == "Model",
                 "import table should resolve names");
    ok &= Expect(package.exports.size() == 1 && package.exports[0].class_name == "Model",
                 "export class reference should resolve through imports");
    ok &= Expect(package.exports.size() == 1 && hp2::IsGeometryCandidate(package.exports[0]),
                 "model export should be selected as a geometry candidate");

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
