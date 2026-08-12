#pragma once

#include "hp2/ue_package.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace hp2 {

enum class Button : std::uint8_t {
    A,
    B,
    X,
    Y,
    L1,
    R1,
    L2,
    R2,
    Start,
    Select,
    LeftStick,
    RightStick,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    Count
};

enum class BootStatus : std::uint8_t {
    WaitingForController,
    MissingGameData,
    IncompatibleGameData,
    PackageProbeReady
};

struct AnalogState {
    float left_x = 0.0f;
    float left_y = 0.0f;
    float right_x = 0.0f;
    float right_y = 0.0f;
    float left_trigger = 0.0f;
    float right_trigger = 0.0f;
    float dpad_x = 0.0f;
    float dpad_y = 0.0f;
};

class Runtime {
public:
    void Initialize(const std::filesystem::path& game_root, std::size_t max_packages = 4096);
    void SetControllerPresent(bool present);
    void SetButton(Button button, bool pressed);
    void SetAnalog(const AnalogState& analog);

    BootStatus status() const;
    bool controller_present() const { return controller_present_; }
    const AnalogState& analog() const { return analog_; }
    const std::vector<PackageSummary>& packages() const { return packages_; }
    std::size_t valid_package_count() const { return valid_package_count_; }
    const std::filesystem::path& game_root() const { return game_root_; }

private:
    std::filesystem::path game_root_;
    std::array<bool, static_cast<std::size_t>(Button::Count)> buttons_{};
    AnalogState analog_{};
    std::vector<PackageSummary> packages_;
    std::size_t valid_package_count_ = 0;
    bool controller_present_ = false;
};

std::string_view ToString(BootStatus status);

}  // namespace hp2
