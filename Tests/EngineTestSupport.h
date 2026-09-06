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
[[nodiscard]] engine::ibl::IblTextures
makeUniformIbl(rhi::Device& device, const glm::vec3& radiance, std::string_view label);

// Writes a self-contained animated glTF fixture into `dir` and returns the .gltf path: a translated
// quad whose node carries one translation channel, so the loader's animation handling can be
// exercised without a fetched asset. `interpolation` selects the sampler mode ("LINEAR", "STEP" or
// "CUBICSPLINE") and, with it, which output accessor the sampler reads. `hierarchy` moves the mesh
// onto a static child of the animated node, so the bake has an ancestor chain to compose. The
// authored node translation deliberately differs from the clip's value at t = 0.
[[nodiscard]] std::filesystem::path writeAnimatedQuadGltf(const std::filesystem::path& dir,
                                                          std::string_view interpolation,
                                                          bool hierarchy = false);

} // namespace lmx::test
