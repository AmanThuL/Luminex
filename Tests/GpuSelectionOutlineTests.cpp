#include "GpuTestSupport.h"

#include "Render/FrameDeclaration.h"
#include "Render/GraphDump.h"
#include "Render/SelectionOutline.h"

#include "SceneTableTestSupport.h"
#include <algorithm>
#include <array>

using lmx::test::FixtureDrawItem;
using lmx::test::FixtureMaterial;
using lmx::test::FixtureMesh;
using lmx::test::fixtureMesh;
using lmx::test::FixtureSceneView;

namespace {
using namespace lmx::render;
using namespace lmx::rhi;

//======================================================================================================================
MeshData outlineQuad() {
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
OutlineFrame outlineFrame(Device& device, TransientPool& pool, Renderer& renderer,
                          SelectionOutline* outline, const FixtureSceneView& view,
                          float backingScale = 1.0f, bool visible = true) {
    Camera camera;
    camera.position = {0, 0, 4};
    camera.fovY = glm::half_pi<float>();
    auto& commands = device.beginFrame();
    const auto prepared = view.prepare(device);
    FrameDeclaration frame(pool, renderer, commands, camera, prepared, true);
    const auto output = outline ? outline->declare(frame.graph(), commands, frame.displayColor(),
                                                   camera, prepared, 0, backingScale, visible)
                                : frame.displayColor();
    frame.graph().exportTexture(output);
    OutlineFrame result;
    result.record = frame.execute();
    device.endFrame(nullptr);
    device.waitIdle();
    result.display.resize(kSize * kSize * 4);
    renderer.colorTarget().readback(result.display.data(), result.display.size());
    if (outline) {
        result.outlined.resize(result.display.size());
        outline->target().readback(result.outlined.data(), result.outlined.size());
    }
    result.scene.resize(kSize * kSize * 4);
    renderer.hdrColorTarget().readback(result.scene.data(), result.scene.size() * sizeof(uint16_t));
    if (view.temporal.enabled) {
        result.history.resize(result.scene.size());
        renderer.historyTarget()->readback(result.history.data(),
                                           result.history.size() * sizeof(uint16_t));
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

//======================================================================================================================
std::unique_ptr<Texture> outlineCutout(Device& device) {
    // Half alpha times the authored 0.5 factor is below 0.3; full alpha times that factor survives.
    // Match the projected 16-pixel width so probes lie on texel centers, away from wrapped
    // linear-filter footprints that legitimately mix the first and last texel at a UV seam.
    std::array<uint8_t, 64> texels;
    texels.fill(255);
    for (size_t texel = 0; texel < 8; ++texel) {
        texels[texel * 4 + 3] = 128;
    }
    const TextureMip mip{.data = texels.data(), .bytesPerRow = 64};
    auto texture = device.createTexture({.width = 16,
                                         .height = 1,
                                         .format = Format::RGBA8Unorm_sRGB,
                                         .sampled = true,
                                         .label = "lmx.test.selection.cutout"},
                                        std::span{&mip, 1});
    INFO(errorOf(texture));
    REQUIRE(texture);
    return std::move(*texture);
}
} // namespace

//======================================================================================================================
TEST_CASE(
    "selection outline changes visible borders but preserves solid interiors and scales thickness",
    "[gpu][selection-outline]") {
    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device);
    TransientPool pool(**device);
    auto renderer = Renderer::create(**device, kSize, kSize, true);
    INFO(errorOf(renderer));
    REQUIRE(renderer);
    auto outline = SelectionOutline::create(**device, kSize, kSize, true);
    INFO(errorOf(outline));
    REQUIRE(outline);
    auto mesh = fixtureMesh(**device, outlineQuad(), "lmx.test.selection.quad");
    REQUIRE(mesh);
    const auto item = outlineItem(*mesh);
    const auto view = outlineView(std::span{&item, 1});
    const auto thin = outlineFrame(**device, pool, **renderer, outline->get(), view);
    REQUIRE(selectionPasses(thin.record) == 3);
    REQUIRE(changedPixels(thin) > 0);
    // The quad projects to [24,40), leaving an 8-pixel interior well away from the border.
    REQUIRE(changedPixels(thin, 28, 28, 36, 36) == 0);
    REQUIRE(changedPixels(thin, 0, 0, 16, 16) == 0);
    REQUIRE(changedPixels(thin, 23, 28, 25, 36) > 0);
    REQUIRE(changedPixels(thin, 21, 28, 22, 36) == 0);
    const auto thick = outlineFrame(**device, pool, **renderer, outline->get(), view, 2.0f);
    REQUIRE(changedPixels(thick) > changedPixels(thin));
    REQUIRE(changedPixels(thick, 21, 28, 22, 36) > 0);
    REQUIRE(changedPixels(thick, 28, 28, 36, 36) == 0);
    REQUIRE(thick.display == thin.display);
}

//======================================================================================================================
TEST_CASE("selection outline respects full and partial foreground occlusion",
          "[gpu][selection-outline]") {
    auto device = createDevice();
    REQUIRE(device);
    TransientPool pool(**device);
    auto renderer = Renderer::create(**device, kSize, kSize, true);
    REQUIRE(renderer);
    auto outline = SelectionOutline::create(**device, kSize, kSize, true);
    INFO(errorOf(outline));
    REQUIRE(outline);
    auto mesh = fixtureMesh(**device, outlineQuad(), "lmx.test.selection.occlusionQuad");
    REQUIRE(mesh);
    std::array items{outlineItem(*mesh), outlineItem(*mesh)};
    items[1].model = glm::translate(glm::mat4{1}, glm::vec3{0, 0, 1}) *
                     glm::scale(glm::mat4{1}, glm::vec3{2, 2, 1});
    items[1].previousModel = items[1].model;
    items[1].material.emissive = {0.3f, 0.1f, 0.1f};
    const auto view = outlineView(items);
    const auto hidden = outlineFrame(**device, pool, **renderer, outline->get(), view);
    REQUIRE(hidden.outlined == hidden.display);
    items[1].model = glm::translate(glm::mat4{1}, glm::vec3{1, 0, 1});
    items[1].previousModel = items[1].model;
    const auto partial = outlineFrame(**device, pool, **renderer, outline->get(), view);
    REQUIRE(changedPixels(partial, 23, 28, 25, 36) > 0);
    // The occlusion cut is inside the object's silhouette, not a selected geometry border.
    REQUIRE(changedPixels(partial, 30, 28, 34, 36) == 0);
    // The selected right edge lies behind the occluder; no hidden geometry border may appear.
    REQUIRE(changedPixels(partial, 36, 20, 44, 44) == 0);
}

//======================================================================================================================
TEST_CASE("selection outline never traces or paints a thin foreground rod",
          "[gpu][selection-outline]") {
    auto device = createDevice();
    REQUIRE(device);
    TransientPool pool(**device);
    auto renderer = Renderer::create(**device, kSize, kSize, true);
    REQUIRE(renderer);
    auto outline = SelectionOutline::create(**device, kSize, kSize, true);
    INFO(errorOf(outline));
    REQUIRE(outline);
    auto mesh = fixtureMesh(**device, outlineQuad(), "lmx.test.selection.rodQuad");
    REQUIRE(mesh);
    std::array items{outlineItem(*mesh), outlineItem(*mesh)};
    // At z=1 this projects to [31,33) x [16,48): a two-pixel rod crossing both natural borders.
    items[1].model = glm::translate(glm::mat4{1}, glm::vec3{0, 0, 1}) *
                     glm::scale(glm::mat4{1}, glm::vec3{0.09375f, 1.5f, 1});
    items[1].previousModel = items[1].model;
    items[1].material.emissive = {0.4f, 0.1f, 0.1f};
    const auto view = outlineView(items);
    for (const float backingScale : std::array{1.0f, 2.0f}) {
        CAPTURE(backingScale);
        const auto frame =
            outlineFrame(**device, pool, **renderer, outline->get(), view, backingScale);
        const auto rod = pixelAt(frame.display, 31, 32);
        const auto selected = pixelAt(frame.display, 28, 32);
        REQUIRE(rod.r > rod.b);
        REQUIRE(selected.b > selected.r);
        // No internal cut-line, and no dilation onto the foreground at either silhouette crossing.
        REQUIRE(changedPixels(frame, 28, 28, 36, 36) == 0);
        REQUIRE(changedPixels(frame, 31, 20, 33, 44) == 0);
        REQUIRE(changedPixels(frame, 23, 28, 25, 36) > 0);
        REQUIRE(changedPixels(frame, 39, 28, 41, 36) > 0);
    }
}

//======================================================================================================================
TEST_CASE("selection outline respects masked foreground cutouts without tracing their interior",
          "[gpu][selection-outline][alpha-mask]") {
    auto device = createDevice();
    REQUIRE(device);
    TransientPool pool(**device);
    auto renderer = Renderer::create(**device, kSize, kSize, true);
    REQUIRE(renderer);
    auto outline = SelectionOutline::create(**device, kSize, kSize, true);
    INFO(errorOf(outline));
    REQUIRE(outline);
    auto mesh = fixtureMesh(**device, outlineQuad(), "lmx.test.selection.maskedForegroundQuad");
    REQUIRE(mesh);
    auto texture = outlineCutout(**device);
    std::array items{outlineItem(*mesh), outlineItem(*mesh)};
    // Foreground covers [16,48) squared; its alpha boundary is at x=32, inside the selected quad.
    items[1].model = glm::translate(glm::mat4{1}, glm::vec3{0, 0, 1}) *
                     glm::scale(glm::mat4{1}, glm::vec3{1.5f, 1.5f, 1});
    items[1].previousModel = items[1].model;
    items[1].material.emissive = {0.4f, 0.1f, 0.1f};
    items[1].material.diffuse = texture.get();
    items[1].material.alphaMode = AlphaMode::Mask;
    items[1].material.albedo.a = 0.5f;
    items[1].material.alphaCutoff = 0.3f;
    const auto view = outlineView(items);
    for (const float backingScale : std::array{1.0f, 2.0f}) {
        CAPTURE(backingScale);
        for (const bool flipped : std::array{false, true}) {
            CAPTURE(flipped);
            items[1].material.uvTransform[0][0] = flipped ? -1.0f : 1.0f;
            items[1].material.uvTransform[3][0] = flipped ? 1.0f : 0.0f;
            const auto frame =
                outlineFrame(**device, pool, **renderer, outline->get(), view, backingScale);
            const auto transparent = pixelAt(frame.display, flipped ? 36 : 28, 32);
            const auto opaque = pixelAt(frame.display, flipped ? 28 : 36, 32);
            REQUIRE(transparent.b > transparent.r);
            REQUIRE(opaque.r > opaque.b);
            REQUIRE(changedPixels(frame, 28, 28, 36, 36) == 0);
            if (flipped) {
                REQUIRE(changedPixels(frame, 39, 28, 41, 36) > 0);
                REQUIRE(changedPixels(frame, 35, 23, 38, 25) > 0);
                REQUIRE(changedPixels(frame, 20, 20, 28, 44) == 0);
            } else {
                REQUIRE(changedPixels(frame, 23, 28, 25, 36) > 0);
                REQUIRE(changedPixels(frame, 26, 23, 29, 25) > 0);
                REQUIRE(changedPixels(frame, 36, 20, 44, 44) == 0);
            }
        }
    }
}

//======================================================================================================================
TEST_CASE("selection outline shares MASK factor cutoff transformed UV and double-sided coverage",
          "[gpu][selection-outline][alpha-mask]") {
    auto device = createDevice();
    REQUIRE(device);
    TransientPool pool(**device);
    auto renderer = Renderer::create(**device, kSize, kSize, true);
    REQUIRE(renderer);
    auto outline = SelectionOutline::create(**device, kSize, kSize, true);
    INFO(errorOf(outline));
    REQUIRE(outline);
    auto mesh = fixtureMesh(**device, outlineQuad(), "lmx.test.selection.maskQuad");
    REQUIRE(mesh);
    auto texture = outlineCutout(**device);
    auto item = outlineItem(*mesh);
    item.material.diffuse = texture.get();
    item.material.alphaMode = AlphaMode::Mask;
    item.material.albedo.a = 0.5f;
    item.material.alphaCutoff = 0.3f;
    const auto view = outlineView(std::span{&item, 1});
    const auto front = outlineFrame(**device, pool, **renderer, outline->get(), view);
    REQUIRE(changedPixels(front, 23, 28, 25, 36) == 0);
    REQUIRE(changedPixels(front, 38, 28, 42, 36) > 0);
    item.material.uvTransform[0][0] = -1;
    item.material.uvTransform[3][0] = 1;
    const auto flipped = outlineFrame(**device, pool, **renderer, outline->get(), view);
    REQUIRE(changedPixels(flipped, 23, 28, 25, 36) > 0);
    REQUIRE(changedPixels(flipped, 38, 28, 42, 36) == 0);
    item.model = glm::rotate(glm::mat4{1}, glm::pi<float>(), glm::vec3{0, 1, 0});
    item.previousModel = item.model;
    const auto culled = outlineFrame(**device, pool, **renderer, outline->get(), view);
    REQUIRE(culled.outlined == culled.display);
    item.material.doubleSided = true;
    const auto back = outlineFrame(**device, pool, **renderer, outline->get(), view);
    REQUIRE(changedPixels(back, 23, 28, 25, 36) == 0);
    REQUIRE(changedPixels(back, 38, 28, 42, 36) > 0);
    item.material.alphaCutoff = 0.6f;
    const auto rejected = outlineFrame(**device, pool, **renderer, outline->get(), view);
    REQUIRE(rejected.outlined == rejected.display);
}

//======================================================================================================================
TEST_CASE(
    "selection presentation leaves ordinary display scene color and temporal history untouched",
    "[gpu][selection-outline][temporal]") {
    auto device = createDevice();
    REQUIRE(device);
    TransientPool referencePool(**device);
    TransientPool outlinedPool(**device);
    auto reference = Renderer::create(**device, kSize, kSize, true);
    REQUIRE(reference);
    auto renderer = Renderer::create(**device, kSize, kSize, true);
    REQUIRE(renderer);
    auto outline = SelectionOutline::create(**device, kSize, kSize, true);
    INFO(errorOf(outline));
    REQUIRE(outline);
    auto mesh = fixtureMesh(**device, outlineQuad(), "lmx.test.selection.historyQuad");
    REQUIRE(mesh);
    auto item = outlineItem(*mesh);
    auto view = outlineView(std::span{&item, 1});
    view.temporal.enabled = true;
    view.temporal.jitterEnabled = true;
    view.temporal.reconstruction = ReconstructionMode::NativeTaa;
    for (uint32_t index = 0; index < 4; ++index) {
        INFO("declared frame " << index);
        const auto plain = outlineFrame(**device, referencePool, **reference, nullptr, view);
        const auto selected =
            outlineFrame(**device, outlinedPool, **renderer, outline->get(), view);
        REQUIRE(selectionPasses(plain.record) == 0);
        REQUIRE(selectionPasses(selected.record) == 3);
        REQUIRE(selected.display == plain.display);
        REQUIRE(selected.scene == plain.scene);
        REQUIRE(selected.history == plain.history);
        REQUIRE(changedPixels(selected) > 0);
        REQUIRE((*renderer)->temporalStatus().historyAge ==
                (*reference)->temporalStatus().historyAge);
    }
}

//======================================================================================================================
TEST_CASE("rejected selection refreshes its UI target without a stale outline",
          "[gpu][selection-outline][visibility]") {
    auto device = createDevice();
    REQUIRE(device);
    TransientPool pool(**device);
    auto renderer = Renderer::create(**device, kSize, kSize, true);
    REQUIRE(renderer);
    auto outline = SelectionOutline::create(**device, kSize, kSize, true);
    REQUIRE(outline);
    auto mesh = fixtureMesh(**device, outlineQuad(), "lmx.test.selection.rejectedQuad");
    REQUIRE(mesh);
    auto item = outlineItem(*mesh);
    const auto view = outlineView(std::span{&item, 1});
    const auto highlighted = outlineFrame(**device, pool, **renderer, outline->get(), view);
    REQUIRE(highlighted.outlined != highlighted.display);
    item.model[3].x = 100;
    const auto rejected =
        outlineFrame(**device, pool, **renderer, outline->get(), view, 1.0f, false);
    REQUIRE(rejected.outlined == rejected.display);
    REQUIRE(rejected.outlined != highlighted.outlined);
    const auto dump = dumpCompiledFrame(rejected.record);
    REQUIRE(dump.find("lmx.pass.selection.passthrough") != std::string::npos);
    REQUIRE(dump.find("lmx.pass.selection.coverage") == std::string::npos);
    REQUIRE(dump.find("lmx.pass.selection.visibility") == std::string::npos);
    item.model[3].x = 0;
    const auto returned = outlineFrame(**device, pool, **renderer, outline->get(), view);
    REQUIRE(returned.outlined == highlighted.outlined);
}
