#include "hp2/runtime.h"

#include <algorithm>
#include <cmath>

namespace hp2 {
namespace {

float ClampAxis(float value) {
    if (!std::isfinite(value)) {
        return 0.0f;
    }
    return std::clamp(value, -1.0f, 1.0f);
}

float ClampTrigger(float value) {
    if (!std::isfinite(value)) {
        return 0.0f;
    }
    return std::clamp(value, 0.0f, 1.0f);
}

}  // namespace

void Runtime::Initialize(const std::filesystem::path& game_root, std::size_t max_packages) {
    game_root_ = game_root;
    packages_ = ScanPackages(game_root_, max_packages);
    valid_package_count_ = static_cast<std::size_t>(std::count_if(
        packages_.begin(), packages_.end(), [](const PackageSummary& package) { return package.valid; }
    ));
}

void Runtime::SetControllerPresent(bool present) {
    controller_present_ = present;
    if (!present) {
        buttons_.fill(false);
        analog_ = {};
    }
}

void Runtime::SetButton(Button button, bool pressed) {
    controller_present_ = true;
    const auto index = static_cast<std::size_t>(button);
    if (index < buttons_.size()) {
        buttons_[index] = pressed;
    }
}

void Runtime::SetAnalog(const AnalogState& analog) {
    controller_present_ = true;
    analog_.left_x = ClampAxis(analog.left_x);
    analog_.left_y = ClampAxis(analog.left_y);
    analog_.right_x = ClampAxis(analog.right_x);
    analog_.right_y = ClampAxis(analog.right_y);
    analog_.left_trigger = ClampTrigger(analog.left_trigger);
    analog_.right_trigger = ClampTrigger(analog.right_trigger);
    analog_.dpad_x = ClampAxis(analog.dpad_x);
    analog_.dpad_y = ClampAxis(analog.dpad_y);
}

BootStatus Runtime::status() const {
    if (!controller_present_) {
        return BootStatus::WaitingForController;
    }
    if (packages_.empty()) {
        return BootStatus::MissingGameData;
    }
    if (valid_package_count_ == 0) {
        return BootStatus::IncompatibleGameData;
    }
    return BootStatus::PackageProbeReady;
}

std::string_view ToString(BootStatus status) {
    switch (status) {
        case BootStatus::WaitingForController: return "waiting-for-controller";
        case BootStatus::MissingGameData: return "missing-game-data";
        case BootStatus::IncompatibleGameData: return "incompatible-game-data";
        case BootStatus::PackageProbeReady: return "package-probe-ready";
    }
    return "unknown";
}

}  // namespace hp2
