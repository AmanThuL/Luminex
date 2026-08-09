#pragma once

#include <glm/vec3.hpp>

#include <filesystem>
#include <optional>
#include <string_view>

namespace lmx::test {

[[nodiscard]] bool near3(const glm::vec3& a, const glm::vec3& b);
[[nodiscard]] std::optional<std::filesystem::path> findRepoAsset(std::string_view relativePath);

} // namespace lmx::test
