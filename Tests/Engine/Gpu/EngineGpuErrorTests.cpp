#include <catch2/catch_test_macros.hpp>

#include "Engine/Gpu/GpuError.h"

//======================================================================================================================
TEST_CASE("gpuError maps InvalidDesc to InvalidArgument and keeps the message", "[engine]") {
    const lmx::Error error = lmx::engine::gpuError({rojoRHI::ErrorCode::InvalidDesc, "bad desc"});
    CHECK(error.code == lmx::ErrorCode::InvalidArgument);
    CHECK(error.message == "bad desc");
}

//======================================================================================================================
TEST_CASE("gpuError maps creation failures to Gpu and DeviceUnsupported to Unsupported",
          "[engine]") {
    CHECK(lmx::engine::gpuError({rojoRHI::ErrorCode::ShaderLoadFailed, "a"}).code ==
          lmx::ErrorCode::Gpu);
    CHECK(lmx::engine::gpuError({rojoRHI::ErrorCode::PipelineCreationFailed, "b"}).code ==
          lmx::ErrorCode::Gpu);
    CHECK(lmx::engine::gpuError({rojoRHI::ErrorCode::ResourceCreationFailed, "c"}).code ==
          lmx::ErrorCode::Gpu);
    CHECK(lmx::engine::gpuError({rojoRHI::ErrorCode::SwapchainFailed, "d"}).code ==
          lmx::ErrorCode::Gpu);
    CHECK(lmx::engine::gpuError({rojoRHI::ErrorCode::DeviceUnsupported, "e"}).code ==
          lmx::ErrorCode::Unsupported);
}
