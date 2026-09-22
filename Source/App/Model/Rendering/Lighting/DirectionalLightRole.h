//----------------------------------------------------------------------------------------------------------------------
/// @file DirectionalLightRole.h
/// @brief Declares the fixed role label the Inspector shows for each directional light index.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include <cstddef>
#include <string_view>

namespace lmx::app {

/// The role label the Inspector's Directional light section shows beside a light's index (spec
/// section 7): light 0 is `Scene::lights`' shadow caster, lights 1 and 2 are unshadowed. Pure and
/// ImGui-free so the pairing is unit-testable without a draw call.
constexpr std::string_view directionalLightRoleLabel(size_t index) {
    return index == 0 ? "Shadow caster" : "Unshadowed";
}

} // namespace lmx::app
