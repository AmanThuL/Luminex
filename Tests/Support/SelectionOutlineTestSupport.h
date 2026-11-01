#pragma once

#include "Support/GpuTestSupport.h"
#include "Support/SceneTableTestSupport.h"

#include "Render/Graph/FrameDeclaration.h"
#include "Render/Graph/GraphDump.h"
#include "Render/Passes/SelectionOutline/SelectionOutline.h"

#include <algorithm>
#include <array>
#include <cstring>

using lmx::test::FixtureDrawItem;
using lmx::test::FixtureMaterial;
using lmx::test::FixtureMesh;
using lmx::test::fixtureMesh;
using lmx::test::FixtureSceneView;

namespace {
using namespace lmx::render;
using namespace rojoRHI;

//======================================================================================================================
lmx::engine::MeshData outlineQuad() {
    return {.vertices = {{-1, -1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1},
                         {1, -1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1},
                         {1, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0},
                         {-1, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0}},
            .indices = {0, 1, 2, 0, 2, 3}};
}

struct OutlineFrame {
    std::vector<uint8_t> display;
    std::vector<uint8_t> outlined;
    std::vector<uint16_t> scene;
    std::vector<uint16_t> history;
    CompiledFrameRecord record;
};

//======================================================================================================================
FixtureSceneView outlineView(std::span<const FixtureDrawItem> items) {
    FixtureSceneView view;
    view.items = items;
    view.boundingSphere = {0, 0, 0, 8};
    for (auto& light : view.lights) {
        light.strength = {0, 0, 0};
    }
    view.bloomEnabled = false;
    return view;
}

//======================================================================================================================
FixtureDrawItem outlineItem(const FixtureMesh& mesh) {
    FixtureDrawItem item;
    item.mesh = &mesh;
    item.material.albedo = {0, 0, 0, 1};
    item.material.emissive = {0.1f, 0.25f, 0.4f};
    return item;
}

//======================================================================================================================
OutlineFrame outlineFrame(lmx::engine::GpuContext& gpu, TransientPool& pool, Renderer& renderer,
                          SelectionOutline* outline, const FixtureSceneView& view,
                          float backingScale = 1.0f, bool visible = true) {
    lmx::engine::Camera camera;
    camera.position = {0, 0, 4};
    camera.fovY = glm::half_pi<float>();
    auto gpuFrame1 = gpu.beginFrame();
    auto& commands = gpuFrame1.commands;
    OutlineFrame result;
    try {
        const auto prepared = view.prepare(gpu);
        FrameDeclaration frame(pool, commands, true);
        const auto declared = frame.declare(renderer, camera, prepared);
        INFO(errorOf(declared));
        REQUIRE(declared);
        const auto output = outline ? outline->declare(frame.graph(), commands, prepared,
                                                       {.display = frame.displayColor(),
                                                        .camera = camera,
                                                        .selectedDraw = 0,
                                                        .backingScale = backingScale,
                                                        .visible = visible})
                                    : frame.displayColor();
        frame.graph().exportTexture(output);
        result.record = frame.execute();
    } catch (...) {
        gpu.submitAndEndFrame(gpuFrame1);
        throw;
    }
    gpu.submitAndEndFrame(gpuFrame1);
    gpu.waitIdle();
    result.display.resize(kSize * kSize * 4);
    {
        const auto readbackBytes =
            lmx::render::readbackNow(gpu, renderer.colorTarget(), renderer.textureStates());
        INFO(errorOf(readbackBytes));
        REQUIRE(readbackBytes);
        REQUIRE(readbackBytes->size() == result.display.size());
        std::memcpy(result.display.data(), readbackBytes->data(), result.display.size());
    }
    if (outline) {
        result.outlined.resize(result.display.size());
        {
            const auto readbackBytes =
                lmx::render::readbackNow(gpu, outline->target(), renderer.textureStates());
            INFO(errorOf(readbackBytes));
            REQUIRE(readbackBytes);
            REQUIRE(readbackBytes->size() == result.outlined.size());
            std::memcpy(result.outlined.data(), readbackBytes->data(), result.outlined.size());
        }
    }
    result.scene.resize(kSize * kSize * 4);
    {
        const auto readbackBytes =
            lmx::render::readbackNow(gpu, renderer.hdrColorTexture(), renderer.textureStates());
        INFO(errorOf(readbackBytes));
        REQUIRE(readbackBytes);
        REQUIRE(readbackBytes->size() == result.scene.size() * sizeof(uint16_t));
        std::memcpy(result.scene.data(), readbackBytes->data(),
                    result.scene.size() * sizeof(uint16_t));
    }
    if (view.temporal.enabled) {
        result.history.resize(result.scene.size());
        {
            const auto* history = renderer.historyTexture();
            REQUIRE(history != nullptr);
            const auto readbackBytes =
                lmx::render::readbackNow(gpu, *history, renderer.textureStates());
            INFO(errorOf(readbackBytes));
            REQUIRE(readbackBytes);
            REQUIRE(readbackBytes->size() == result.history.size() * sizeof(uint16_t));
            std::memcpy(result.history.data(), readbackBytes->data(),
                        result.history.size() * sizeof(uint16_t));
        }
    }
    return result;
}

//======================================================================================================================
size_t changedPixels(const OutlineFrame& frame, uint32_t left = 0, uint32_t top = 0,
                     uint32_t right = kSize, uint32_t bottom = kSize) {
    size_t changed = 0;
    for (uint32_t y = top; y < bottom; ++y) {
        for (uint32_t x = left; x < right; ++x) {
            const size_t offset = (size_t{y} * kSize + x) * 4;
            bool different = false;
            for (size_t channel = 0; channel < 4; ++channel) {
                different |= frame.display[offset + channel] != frame.outlined[offset + channel];
            }
            changed += different ? 1 : 0;
        }
    }
    return changed;
}

//======================================================================================================================
size_t selectionPasses(const CompiledFrameRecord& record) {
    return static_cast<size_t>(
        std::count_if(record.debug.passes.begin(), record.debug.passes.end(), [](const auto& pass) {
            return pass.label.starts_with("lmx.pass.selection.");
        }));
}

} // namespace
