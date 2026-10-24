#include <catch2/catch_test_macros.hpp>

#include "Engine/Gpu/GpuError.h"

#include <array>

//======================================================================================================================
TEST_CASE("gpuError keeps v2 error messages", "[engine]") {
    for (const auto category : std::array{
             rojoRHI::ErrorCategory::Unsupported, rojoRHI::ErrorCategory::OutOfMemory,
             rojoRHI::ErrorCategory::DriverFailure, rojoRHI::ErrorCategory::ShaderAbiMismatch}) {
        const auto error = lmx::engine::gpuError({category, "bad desc"});
        CHECK(error.message == "bad desc");
    }
}

//======================================================================================================================
TEST_CASE("gpuError maps driver failures to Gpu and Unsupported to Unsupported", "[engine]") {
    CHECK(lmx::engine::gpuError({rojoRHI::ErrorCategory::Unsupported, "unsupported"}).code ==
          lmx::ErrorCode::Unsupported);
    for (const auto category :
         std::array{rojoRHI::ErrorCategory::OutOfMemory, rojoRHI::ErrorCategory::DriverFailure,
                    rojoRHI::ErrorCategory::ShaderAbiMismatch}) {
        CHECK(lmx::engine::gpuError({category, "failure"}).code == lmx::ErrorCode::Gpu);
    }
}
