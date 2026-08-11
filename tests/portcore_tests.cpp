#include "hp2/runtime.h"
#include "hp2/ue_package.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

void WriteU16(std::array<std::uint8_t, 96>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
}

void WriteU32(std::array<std::uint8_t, 96>& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>((value >> (index * 8u)) & 0xffu);
    }
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
    std::filesystem::create_directories(root / "System");

    std::array<std::uint8_t, 96> bytes{};
    WriteU32(bytes, 0, hp2::kUnrealPackageTag);
    WriteU16(bytes, 4, 100);
    WriteU16(bytes, 6, 7);
    WriteU32(bytes, 8, 0);
    WriteU32(bytes, 12, 1);
    WriteU32(bytes, 16, 40);
    WriteU32(bytes, 20, 1);
    WriteU32(bytes, 24, 56);
    WriteU32(bytes, 28, 1);
    WriteU32(bytes, 32, 72);

    const auto valid_path = root / "System" / "Synthetic.u";
    {
        std::ofstream output(valid_path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    bool ok = true;
    const auto summary = hp2::ProbePackage(valid_path);
    ok &= Expect(summary.valid, "synthetic package should be valid");
    ok &= Expect(summary.file_version == 100, "file version should be decoded as little-endian");
    ok &= Expect(summary.licensee_version == 7, "licensee version should be decoded");
    ok &= Expect(summary.name_count == 1 && summary.import_count == 1 && summary.export_count == 1,
                 "table counts should be decoded");

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
    const auto invalid_path = root / "System" / "Broken.utx";
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
