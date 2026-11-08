#include <catch2/catch_test_macros.hpp>

#include "Engine/Gpu/GpuContext.h"

#include <array>
#include <vector>

//======================================================================================================================
TEST_CASE("shaderLibraryPath prefixes the shader directory", "[engine]") {
    CHECK(lmx::engine::shaderLibraryPath("Bloom") == "Shaders/Bloom" LMX_SHADER_LIBRARY_EXTENSION);
}

//======================================================================================================================
TEST_CASE("copyPassTimings copies labels and milliseconds in order", "[engine]") {
    const std::array<rojoRHI::ScopeTiming, 3> source = {
        {{"lmx.frame", 4, rojoRHI::QueueType::Graphics, 0, 2.0},
         {"shadow", 4, rojoRHI::QueueType::Graphics, 1, 0.5},
         {"lighting", 4, rojoRHI::QueueType::Graphics, 1, 1.25}}};
    std::vector<lmx::engine::PassTiming> destination;
    double frameMs = 0;
    uint64_t frame = 0;
    lmx::engine::collectPassTimings(source, destination, frameMs, frame);
    REQUIRE(destination.size() == 2);
    CHECK(destination[0].label == "shadow");
    CHECK(destination[0].gpuMilliseconds == 0.5);
    CHECK(destination[1].label == "lighting");
    CHECK(destination[1].gpuMilliseconds == 1.25);
    CHECK(frame == 4);
}

//======================================================================================================================
TEST_CASE("copyPassTimings reuses the destination", "[engine]") {
    const std::string longLabel(256, 'x');
    const std::array<rojoRHI::ScopeTiming, 2> source = {
        {{"lmx.frame", 4, rojoRHI::QueueType::Graphics, 0, 2.0},
         {longLabel, 4, rojoRHI::QueueType::Graphics, 1, 0.5}}};
    std::vector<lmx::engine::PassTiming> destination;
    double frameMs = 0;
    uint64_t frame = 0;
    lmx::engine::collectPassTimings(source, destination, frameMs, frame);
    const auto capacity = destination.capacity();
    const auto labelCapacity = destination[0].label.capacity();
    const char* const labelStorage = destination[0].label.data();
    lmx::engine::collectPassTimings(source, destination, frameMs, frame);
    REQUIRE(destination.size() == 1);
    CHECK(destination.capacity() == capacity);
    CHECK(destination[0].label.capacity() == labelCapacity);
    CHECK(destination[0].label.data() == labelStorage);
    lmx::engine::collectPassTimings({}, destination, frameMs, frame);
    CHECK(destination.empty());
    CHECK(frame == 0);
    CHECK(frameMs == 0);
}

//======================================================================================================================
TEST_CASE("toUpscalerSupport copies availability, range and name", "[engine]") {
    const std::array support{
        rojoRHI::UpscalerSupport{.kind = rojoRHI::UpscalerKind::FSR, .minInputScale = 0.25f},
        rojoRHI::UpscalerSupport{
            .kind = rojoRHI::UpscalerKind::MetalFX, .minInputScale = 0.5f, .maxInputScale = 1.0f}};
    const auto info = lmx::engine::toUpscalerSupport(support);
    CHECK(info.available);
    CHECK(info.minInputScale == 0.5f);
    CHECK(info.maxInputScale == 1.0f);
    CHECK(info.name == "MetalFX Temporal");
    const auto absent = lmx::engine::toUpscalerSupport(std::span(support).first(1));
    CHECK_FALSE(absent.available);
    CHECK(absent.name.empty());
    CHECK(absent.minInputScale == 0);
    CHECK(absent.maxInputScale == 0);
}

//======================================================================================================================
TEST_CASE("collectPassTimings keeps depth-one rows under the frame scope", "[engine]") {
    using rojoRHI::QueueType;
    const std::array<rojoRHI::ScopeTiming, 10> source = {
        {{"other", 9, QueueType::Graphics, 0, 1},
         {"outside", 9, QueueType::Graphics, 1, 1},
         {"lmx.frame", 9, QueueType::Graphics, 0, 8},
         {"first", 9, QueueType::Graphics, 1, 2},
         {"nested", 9, QueueType::Graphics, 2, 1},
         {"copy", 9, QueueType::Copy, 0, 1},
         {"second", 9, QueueType::Graphics, 1, 3},
         {"wrong-frame", 10, QueueType::Graphics, 1, 4},
         {"platform", 9, QueueType::Graphics, 0, 1},
         {"platform-child", 9, QueueType::Graphics, 1, 1}}};
    std::vector<lmx::engine::PassTiming> rows;
    double frameMs = 0;
    uint64_t frame = 0;
    lmx::engine::collectPassTimings(source, rows, frameMs, frame);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].label == "first");
    CHECK(rows[1].label == "second");
    CHECK(frame == 9);
}

//======================================================================================================================
TEST_CASE("collectPassTimings reports the frame scope as frameGpuMs", "[engine]") {
    const std::array<rojoRHI::ScopeTiming, 2> source = {
        {{"lmx.frame", 7, rojoRHI::QueueType::Graphics, 0, 8},
         {"pass", 7, rojoRHI::QueueType::Graphics, 1, 2}}};
    std::vector<lmx::engine::PassTiming> rows;
    double frameMs = 0;
    uint64_t frame = 0;
    lmx::engine::collectPassTimings(source, rows, frameMs, frame);
    CHECK(frameMs == 8);
    CHECK(frame == 7);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].gpuMilliseconds == 2);
}

//======================================================================================================================
TEST_CASE("a frame closed with no present submits its list", "[engine][device]") {
    lmx::engine::GpuContextDesc desc;
#ifndef NDEBUG
    desc.validation = true;
#endif
    auto gpu = lmx::engine::GpuContext::create(desc);
    REQUIRE(gpu);
    auto first = (*gpu)->beginFrame();
    CHECK(first.number == 1);
    (*gpu)->submitAndEndFrame(first);
    auto second = (*gpu)->beginFrame();
    CHECK(second.number == 2);
    (*gpu)->submitAndEndFrame(second);
    (*gpu)->waitIdle();
    CHECK((*gpu)->completedFrame() == 2);
    CHECK_FALSE((*gpu)->inFrame());
}
