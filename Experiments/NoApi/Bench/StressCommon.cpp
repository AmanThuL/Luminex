//----------------------------------------------------------------------------------------------------------------------
/// @file StressCommon.cpp
/// @brief Implements StressCommon.h.
//----------------------------------------------------------------------------------------------------------------------

#include "Bench/StressCommon.h"

#include <algorithm>

namespace lmx::noapi::bench {

//======================================================================================================================
std::vector<uint8_t> hazardExpectedRgba(const workload::HazardCase& hazardCase, uint32_t extent) {
    std::vector<uint8_t> bytes(uint64_t{extent} * extent * 4);
    for (uint32_t texelIndex = 0; texelIndex < extent * extent; ++texelIndex) {
        const uint8_t value = workload::hazardExpectedTexel(hazardCase, texelIndex);
        uint8_t* texel = bytes.data() + uint64_t{texelIndex} * 4;
        texel[0] = value;
        texel[1] = value;
        texel[2] = value;
        texel[3] = 255;
    }
    return bytes;
}

//======================================================================================================================
std::vector<uint8_t> hazardOldRgba(const workload::HazardCase& hazardCase, uint32_t extent) {
    std::vector<uint8_t> bytes = hazardExpectedRgba(hazardCase, extent);
    for (uint32_t texelIndex = 0; texelIndex < extent * extent; ++texelIndex) {
        uint8_t* texel = bytes.data() + uint64_t{texelIndex} * 4;
        texel[0] = static_cast<uint8_t>(~texel[0]);
        texel[1] = static_cast<uint8_t>(~texel[1]);
        texel[2] = static_cast<uint8_t>(~texel[2]);
        texel[3] = 255;
    }
    return bytes;
}

//======================================================================================================================
int64_t firstRgbMismatch(const std::vector<uint8_t>& expected, const std::vector<uint8_t>& actual) {
    const uint64_t count = std::min(expected.size(), actual.size());
    for (uint64_t i = 0; i + 4 <= count; i += 4) {
        if (expected[i] != actual[i] || expected[i + 1] != actual[i + 1] ||
            expected[i + 2] != actual[i + 2]) {
            return static_cast<int64_t>(i);
        }
    }
    return expected.size() == actual.size() ? -1 : static_cast<int64_t>(count);
}

} // namespace lmx::noapi::bench
