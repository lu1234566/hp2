#pragma once

#include "hp2/ue_animation.h"

#include <cstdint>
#include <vector>

namespace hp2 {

HP2AnimationData ParseHP2AnimationNative(
    const PackageIndex& package,
    const std::string& object_name,
    const std::vector<std::uint8_t>& native_bytes
);

}  // namespace hp2
