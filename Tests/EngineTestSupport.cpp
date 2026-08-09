#include "EngineTestSupport.h"

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

} // namespace lmx::test
