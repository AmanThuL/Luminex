#include "Support/SelectionOutlineTestSupport.h"

namespace {
//======================================================================================================================
lmx::engine::GpuTexture outlineCutout(Device& device) {
    // Half alpha times the authored 0.5 factor is below 0.3; full alpha times that factor survives.
    // Match the projected 16-pixel width so probes lie on texel centers, away from wrapped
    // linear-filter footprints that legitimately mix the first and last texel at a UV seam.
    std::array<uint8_t, 64> texels;
    texels.fill(255);
    for (size_t texel = 0; texel < 8; ++texel) {
        texels[texel * 4 + 3] = 128;
    }
    const TextureData mip{.bytes = std::as_bytes(std::span{texels}), .bytesPerRow = 64};
    auto texture = lmx::engine::GpuTexture::create(device,
                                                   {.format = Format::RGBA8Unorm_sRGB,
                                                    .extent = {16, 1, 1},
                                                    .usage = TextureUsage::Sampled,
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
    auto gpu = createTestGpu();
    TransientPool pool(*gpu);
    auto renderer = Renderer::create(*gpu, kSize, kSize, true);
    INFO(errorOf(renderer));
    REQUIRE(renderer);
    auto outline = SelectionOutline::create(*gpu, kSize, kSize, true);
    INFO(errorOf(outline));
    REQUIRE(outline);
    auto mesh = fixtureMesh(gpu->device(), outlineQuad(), "lmx.test.selection.quad");
    REQUIRE(mesh);
    const auto item = outlineItem(*mesh);
    const auto view = outlineView(std::span{&item, 1});
    const auto thin = outlineFrame(*gpu, pool, **renderer, outline->get(), view);
    REQUIRE(selectionPasses(thin.record) == 3);
    REQUIRE(changedPixels(thin) > 0);
    // The quad projects to [24,40), leaving an 8-pixel interior well away from the border.
    REQUIRE(changedPixels(thin, 28, 28, 36, 36) == 0);
    REQUIRE(changedPixels(thin, 0, 0, 16, 16) == 0);
    REQUIRE(changedPixels(thin, 23, 28, 25, 36) > 0);
    REQUIRE(changedPixels(thin, 21, 28, 22, 36) == 0);
    const auto thick = outlineFrame(*gpu, pool, **renderer, outline->get(), view, 2.0f);
    REQUIRE(changedPixels(thick) > changedPixels(thin));
    REQUIRE(changedPixels(thick, 21, 28, 22, 36) > 0);
    REQUIRE(changedPixels(thick, 28, 28, 36, 36) == 0);
    REQUIRE(thick.display == thin.display);
}

//======================================================================================================================
TEST_CASE("selection outline respects full and partial foreground occlusion",
          "[gpu][selection-outline]") {
    auto gpu = createTestGpu();
    TransientPool pool(*gpu);
    auto renderer = Renderer::create(*gpu, kSize, kSize, true);
    REQUIRE(renderer);
    auto outline = SelectionOutline::create(*gpu, kSize, kSize, true);
    INFO(errorOf(outline));
    REQUIRE(outline);
    auto mesh = fixtureMesh(gpu->device(), outlineQuad(), "lmx.test.selection.occlusionQuad");
    REQUIRE(mesh);
    std::array items{outlineItem(*mesh), outlineItem(*mesh)};
    items[1].model = glm::translate(glm::mat4{1}, glm::vec3{0, 0, 1}) *
                     glm::scale(glm::mat4{1}, glm::vec3{2, 2, 1});
    items[1].previousModel = items[1].model;
    items[1].material.emissive = {0.3f, 0.1f, 0.1f};
    const auto view = outlineView(items);
    const auto hidden = outlineFrame(*gpu, pool, **renderer, outline->get(), view);
    REQUIRE(hidden.outlined == hidden.display);
    items[1].model = glm::translate(glm::mat4{1}, glm::vec3{1, 0, 1});
    items[1].previousModel = items[1].model;
    const auto partial = outlineFrame(*gpu, pool, **renderer, outline->get(), view);
    REQUIRE(changedPixels(partial, 23, 28, 25, 36) > 0);
    // The occlusion cut is inside the object's silhouette, not a selected geometry border.
    REQUIRE(changedPixels(partial, 30, 28, 34, 36) == 0);
    // The selected right edge lies behind the occluder; no hidden geometry border may appear.
    REQUIRE(changedPixels(partial, 36, 20, 44, 44) == 0);
}

//======================================================================================================================
TEST_CASE("selection outline never traces or paints a thin foreground rod",
          "[gpu][selection-outline]") {
    auto gpu = createTestGpu();
    TransientPool pool(*gpu);
    auto renderer = Renderer::create(*gpu, kSize, kSize, true);
    REQUIRE(renderer);
    auto outline = SelectionOutline::create(*gpu, kSize, kSize, true);
    INFO(errorOf(outline));
    REQUIRE(outline);
    auto mesh = fixtureMesh(gpu->device(), outlineQuad(), "lmx.test.selection.rodQuad");
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
        const auto frame = outlineFrame(*gpu, pool, **renderer, outline->get(), view, backingScale);
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
    auto gpu = createTestGpu();
    TransientPool pool(*gpu);
    auto renderer = Renderer::create(*gpu, kSize, kSize, true);
    REQUIRE(renderer);
    auto outline = SelectionOutline::create(*gpu, kSize, kSize, true);
    INFO(errorOf(outline));
    REQUIRE(outline);
    auto mesh =
        fixtureMesh(gpu->device(), outlineQuad(), "lmx.test.selection.maskedForegroundQuad");
    REQUIRE(mesh);
    auto texture = outlineCutout(gpu->device());
    std::array items{outlineItem(*mesh), outlineItem(*mesh)};
    // Foreground covers [16,48) squared; its alpha boundary is at x=32, inside the selected quad.
    items[1].model = glm::translate(glm::mat4{1}, glm::vec3{0, 0, 1}) *
                     glm::scale(glm::mat4{1}, glm::vec3{1.5f, 1.5f, 1});
    items[1].previousModel = items[1].model;
    items[1].material.emissive = {0.4f, 0.1f, 0.1f};
    items[1].material.diffuse = &texture;
    items[1].material.alphaMode = lmx::engine::AlphaMode::Mask;
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
                outlineFrame(*gpu, pool, **renderer, outline->get(), view, backingScale);
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
    auto gpu = createTestGpu();
    TransientPool pool(*gpu);
    auto renderer = Renderer::create(*gpu, kSize, kSize, true);
    REQUIRE(renderer);
    auto outline = SelectionOutline::create(*gpu, kSize, kSize, true);
    INFO(errorOf(outline));
    REQUIRE(outline);
    auto mesh = fixtureMesh(gpu->device(), outlineQuad(), "lmx.test.selection.maskQuad");
    REQUIRE(mesh);
    auto texture = outlineCutout(gpu->device());
    auto item = outlineItem(*mesh);
    item.material.diffuse = &texture;
    item.material.alphaMode = lmx::engine::AlphaMode::Mask;
    item.material.albedo.a = 0.5f;
    item.material.alphaCutoff = 0.3f;
    const auto view = outlineView(std::span{&item, 1});
    const auto front = outlineFrame(*gpu, pool, **renderer, outline->get(), view);
    REQUIRE(changedPixels(front, 23, 28, 25, 36) == 0);
    REQUIRE(changedPixels(front, 38, 28, 42, 36) > 0);
    item.material.uvTransform[0][0] = -1;
    item.material.uvTransform[3][0] = 1;
    const auto flipped = outlineFrame(*gpu, pool, **renderer, outline->get(), view);
    REQUIRE(changedPixels(flipped, 23, 28, 25, 36) > 0);
    REQUIRE(changedPixels(flipped, 38, 28, 42, 36) == 0);
    item.model = glm::rotate(glm::mat4{1}, glm::pi<float>(), glm::vec3{0, 1, 0});
    item.previousModel = item.model;
    const auto culled = outlineFrame(*gpu, pool, **renderer, outline->get(), view);
    REQUIRE(culled.outlined == culled.display);
    item.material.doubleSided = true;
    const auto back = outlineFrame(*gpu, pool, **renderer, outline->get(), view);
    REQUIRE(changedPixels(back, 23, 28, 25, 36) == 0);
    REQUIRE(changedPixels(back, 38, 28, 42, 36) > 0);
    item.material.alphaCutoff = 0.6f;
    const auto rejected = outlineFrame(*gpu, pool, **renderer, outline->get(), view);
    REQUIRE(rejected.outlined == rejected.display);
}

//======================================================================================================================
TEST_CASE("rejected selection refreshes its UI target without a stale outline",
          "[gpu][selection-outline][visibility]") {
    auto gpu = createTestGpu();
    TransientPool pool(*gpu);
    auto renderer = Renderer::create(*gpu, kSize, kSize, true);
    REQUIRE(renderer);
    auto outline = SelectionOutline::create(*gpu, kSize, kSize, true);
    REQUIRE(outline);
    auto mesh = fixtureMesh(gpu->device(), outlineQuad(), "lmx.test.selection.rejectedQuad");
    REQUIRE(mesh);
    auto item = outlineItem(*mesh);
    const auto view = outlineView(std::span{&item, 1});
    const auto highlighted = outlineFrame(*gpu, pool, **renderer, outline->get(), view);
    REQUIRE(highlighted.outlined != highlighted.display);
    item.model[3].x = 100;
    const auto rejected = outlineFrame(*gpu, pool, **renderer, outline->get(), view, 1.0f, false);
    REQUIRE(rejected.outlined == rejected.display);
    REQUIRE(rejected.outlined != highlighted.outlined);
    const auto dump = dumpCompiledFrame(rejected.record);
    REQUIRE(dump.find("lmx.pass.selection.passthrough") != std::string::npos);
    REQUIRE(dump.find("lmx.pass.selection.coverage") == std::string::npos);
    REQUIRE(dump.find("lmx.pass.selection.visibility") == std::string::npos);
    item.model[3].x = 0;
    const auto returned = outlineFrame(*gpu, pool, **renderer, outline->get(), view);
    REQUIRE(returned.outlined == highlighted.outlined);
}

//======================================================================================================================
TEST_CASE("disabled selection and foreground contribute no outline coverage",
          "[gpu][selection-outline][disabled-instance]") {
    auto gpu = createTestGpu();
    TransientPool pool(*gpu);
    auto renderer = Renderer::create(*gpu, kSize, kSize, true);
    REQUIRE(renderer);
    auto outline = SelectionOutline::create(*gpu, kSize, kSize, true);
    REQUIRE(outline);
    auto mesh = fixtureMesh(gpu->device(), outlineQuad(), "lmx.test.selection.disabled");
    REQUIRE(mesh);
    std::array items{outlineItem(*mesh), outlineItem(*mesh)};
    items[1].model = glm::translate(glm::mat4(1), glm::vec3(0, 0, 1));
    items[1].enabled = false;
    const auto view = outlineView(items);
    const auto behindDisabled = outlineFrame(*gpu, pool, **renderer, outline->get(), view);
    REQUIRE(changedPixels(behindDisabled) > 0);
    items[1].model[3].x = 100;
    items[1].enabled = true;
    const auto unobstructed = outlineFrame(*gpu, pool, **renderer, outline->get(), view);
    REQUIRE(unobstructed.outlined == behindDisabled.outlined);
    items[0].enabled = false;
    const auto disabled = outlineFrame(*gpu, pool, **renderer, outline->get(), view);
    REQUIRE(disabled.outlined == disabled.display);
    REQUIRE(selectionPasses(disabled.record) == 1);
    REQUIRE(view.state->scene.objects.size() == 2);
    items[0].enabled = true;
    const auto restored = outlineFrame(*gpu, pool, **renderer, outline->get(), view);
    REQUIRE(restored.outlined == unobstructed.outlined);
}
