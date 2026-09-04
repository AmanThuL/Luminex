#include <catch2/catch_test_macros.hpp>

#include "App/DirectionalLightRole.h"

using namespace lmx::app;

//======================================================================================================================
TEST_CASE("light 0 is the shadow caster", "[app]") {
    REQUIRE(directionalLightRoleLabel(0) == "Shadow caster");
}

//======================================================================================================================
TEST_CASE("lights 1 and 2 are unshadowed", "[app]") {
    REQUIRE(directionalLightRoleLabel(1) == "Unshadowed");
    REQUIRE(directionalLightRoleLabel(2) == "Unshadowed");
}
