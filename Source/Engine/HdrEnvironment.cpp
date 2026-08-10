//----------------------------------------------------------------------------------------------------------------------
/// @file HdrEnvironment.cpp
/// @brief Implements Radiance HDR decoding and deterministic environment-map conversion.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/HdrEnvironment.h"

#include <stb/stb_image.h>

#include <glm/common.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace lmx::engine {

namespace {

//======================================================================================================================
std::unexpected<AssetError> fail(std::string_view path, std::string message,
                                 AssetErrorCode code = AssetErrorCode::Malformed) {
    return std::unexpected(
        AssetError{code, "Radiance HDR '" + std::string(path) + "': " + std::move(message)});
}

//======================================================================================================================
bool validRadiance(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
           value.x >= 0.0f && value.y >= 0.0f && value.z >= 0.0f;
}

//======================================================================================================================
uint32_t wrapX(int64_t x, uint32_t width) {
    const int64_t signedWidth = static_cast<int64_t>(width);
    const int64_t wrapped = ((x % signedWidth) + signedWidth) % signedWidth;
    return static_cast<uint32_t>(wrapped);
}

//======================================================================================================================
glm::vec3 sampleBilinear(const HdrEquirectangularImage& image, const glm::vec3& direction) {
    float longitude = std::atan2(direction.z, direction.x) / glm::two_pi<float>() + 0.5f;
    longitude -= std::floor(longitude);
    const float latitude = std::acos(std::clamp(direction.y, -1.0f, 1.0f)) / glm::pi<float>();

    const float sourceX = longitude * static_cast<float>(image.width) - 0.5f;
    const float sourceY = latitude * static_cast<float>(image.height) - 0.5f;
    const int64_t x0 = static_cast<int64_t>(std::floor(sourceX));
    const int64_t y0 = static_cast<int64_t>(std::floor(sourceY));
    const uint32_t x1 = wrapX(x0 + 1, image.width);
    const uint32_t wrappedX0 = wrapX(x0, image.width);
    const uint32_t clampedY0 =
        static_cast<uint32_t>(std::clamp<int64_t>(y0, 0, static_cast<int64_t>(image.height) - 1));
    const uint32_t clampedY1 = static_cast<uint32_t>(
        std::clamp<int64_t>(y0 + 1, 0, static_cast<int64_t>(image.height) - 1));
    const float blendX = sourceX - std::floor(sourceX);
    const float blendY = sourceY - std::floor(sourceY);
    const auto texel = [&](uint32_t x, uint32_t y) -> const glm::vec3& {
        return image.radiance[size_t{y} * image.width + x];
    };
    const glm::vec3 top = glm::mix(texel(wrappedX0, clampedY0), texel(x1, clampedY0), blendX);
    const glm::vec3 bottom = glm::mix(texel(wrappedX0, clampedY1), texel(x1, clampedY1), blendX);
    return glm::mix(top, bottom, blendY);
}

} // namespace

//======================================================================================================================
AssetResult<HdrEquirectangularImage> loadRadianceHdr(std::string_view path) {
    std::ifstream input(std::string(path), std::ios::binary | std::ios::ate);
    if (!input) {
        return fail(path, "failed to open file", AssetErrorCode::NotFound);
    }
    const std::streamoff end = input.tellg();
    if (end <= 0 || end > static_cast<std::streamoff>(std::numeric_limits<int>::max())) {
        return fail(path, "file is empty or too large to decode", AssetErrorCode::Io);
    }
    std::vector<unsigned char> bytes(static_cast<size_t>(end));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), end);
    if (!input) {
        return fail(path, "failed to read file", AssetErrorCode::Io);
    }
    const int byteCount = static_cast<int>(bytes.size());
    if (stbi_is_hdr_from_memory(bytes.data(), byteCount) == 0) {
        return fail(path, "input is not a Radiance HDR image", AssetErrorCode::Unsupported);
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    std::unique_ptr<float[], decltype(&stbi_image_free)> decoded(
        stbi_loadf_from_memory(bytes.data(), byteCount, &width, &height, &channels, 3),
        &stbi_image_free);
    if (decoded == nullptr) {
        const char* reason = stbi_failure_reason();
        return fail(path, reason != nullptr ? reason : "decoder rejected the image");
    }
    if (width <= 0 || height <= 0 || int64_t{width} != int64_t{height} * 2) {
        return fail(path, "expected a non-empty 2:1 equirectangular image");
    }

    HdrEquirectangularImage image;
    image.width = static_cast<uint32_t>(width);
    image.height = static_cast<uint32_t>(height);
    image.radiance.resize(size_t{image.width} * image.height);
    for (size_t texel = 0; texel < image.radiance.size(); ++texel) {
        const glm::vec3 value(decoded[texel * 3], decoded[texel * 3 + 1], decoded[texel * 3 + 2]);
        if (!validRadiance(value)) {
            return fail(path, "decoded radiance contains a negative or non-finite value");
        }
        image.radiance[texel] = value;
    }
    return image;
}

//======================================================================================================================
AssetResult<ibl::CpuCubemap> equirectangularToCubemap(const HdrEquirectangularImage& image,
                                                      uint32_t faceSize, float yawRadians,
                                                      float radianceScale) {
    if (image.width == 0 || image.height == 0 ||
        uint64_t{image.width} != uint64_t{image.height} * 2 ||
        image.radiance.size() != size_t{image.width} * image.height) {
        return fail("memory", "expected a complete non-empty 2:1 equirectangular image");
    }
    if (faceSize == 0) {
        return fail("memory", "cubemap face size must be at least one");
    }
    if (!std::isfinite(yawRadians) || !std::isfinite(radianceScale) || radianceScale < 0.0f) {
        return fail("memory",
                    "yaw must be finite and radiance scale must be finite and non-negative");
    }
    if (!std::all_of(image.radiance.begin(), image.radiance.end(), validRadiance)) {
        return fail("memory", "source radiance contains a negative or non-finite value");
    }

    const float sine = std::sin(yawRadians);
    const float cosine = std::cos(yawRadians);
    ibl::CpuCubemap cube;
    cube.faceSize = faceSize;
    for (uint32_t face = 0; face < ibl::kCubeFaceCount; ++face) {
        cube.faces[face].resize(size_t{faceSize} * faceSize);
        for (uint32_t y = 0; y < faceSize; ++y) {
            for (uint32_t x = 0; x < faceSize; ++x) {
                const glm::vec3 direction = ibl::faceDirection(face, x, y, faceSize);
                const glm::vec3 rotated(cosine * direction.x - sine * direction.z, direction.y,
                                        sine * direction.x + cosine * direction.z);
                cube.faces[face][size_t{y} * faceSize + x] =
                    glm::vec4(sampleBilinear(image, rotated) * radianceScale, 1.0f);
            }
        }
    }
    return cube;
}

} // namespace lmx::engine
