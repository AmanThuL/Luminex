#include "EngineTestSupport.h"

#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/epsilon.hpp>

namespace lmx::test {

namespace {
constexpr float kEpsilon = 1e-4f;
}

//======================================================================================================================
bool near3(const glm::vec3& a, const glm::vec3& b) {
    return glm::all(glm::epsilonEqual(a, b, kEpsilon));
}

//======================================================================================================================
std::optional<std::filesystem::path> findRepoAsset(std::string_view relativePath) {
    std::filesystem::path directory = std::filesystem::current_path();
    for (int depth = 0; depth < 8; ++depth) {
        if (std::filesystem::path candidate = directory / relativePath;
            std::filesystem::exists(candidate)) {
            return candidate;
        }
        const std::filesystem::path parent = directory.parent_path();
        if (parent == directory) {
            break;
        }
        directory = parent;
    }
    return std::nullopt;
}

//======================================================================================================================
engine::ibl::IblTextures makeUniformIbl(rhi::Device& device, const glm::vec3& radiance,
                                        std::string_view label) {
    // Face size 1: a constant environment carries no detail for a larger source to hold, and the
    // generators' output extents are fixed by engine::ibl regardless of what they read from.
    auto generated =
        engine::ibl::generate(device, engine::ibl::makeConstantCubemap(radiance, 1), label);
    REQUIRE(generated.has_value());
    return std::move(*generated);
}

} // namespace lmx::test
