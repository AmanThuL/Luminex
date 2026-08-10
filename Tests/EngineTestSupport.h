#pragma once

#include "Engine/Ibl.h"
#include "RHI/RHI.h"

#include <glm/vec3.hpp>

#include <filesystem>
#include <optional>
#include <string_view>

namespace lmx::test {

[[nodiscard]] bool near3(const glm::vec3& a, const glm::vec3& b);
[[nodiscard]] std::optional<std::filesystem::path> findRepoAsset(std::string_view relativePath);

// The uploaded IBL set of a uniform environment, generated through the production path rather than
// hand-built: engine::ibl's irradiance convolution and prefilter both reproduce a constant
// environment exactly at every roughness, so a test that binds this knows both image-based samples
// are `radiance` itself and can state its expectation in closed form. Building it through the real
// generators is also what keeps a probe honest about the assets the renderer actually samples --
// their face orientation, their mip chain, and their RG16Float DFG storage included.
[[nodiscard]] engine::ibl::IblTextures makeUniformIbl(rhi::Device& device,
                                                      const glm::vec3& radiance,
                                                      std::string_view label);

} // namespace lmx::test
