#include "EngineSceneTestSupport.h"
#include "Render/SceneViewBuilder.h"
#include "Scenes/CatalogScenes.h"

//======================================================================================================================
// Deterministic diagnostics with an internal neutral-environment fallback, so this loads with no
// fetched-asset gate at all, unlike the Sponza/Helmet cases above.
TEST_CASE("loadMaterialLabScene builds deterministic diagnostics without requiring fetched assets",
          "[gpu]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device.has_value());
    auto scene = lmx::scenes::loadMaterialLabScene(**device);
    INFO(describeSceneError(scene));
    REQUIRE(scene.has_value());

    // 5x5 sphere grid (25) + 6 colour patches + 1 gradient ramp + 1 normal-map probe + 3 depth
    // probes + 1 mip probe.
    REQUIRE((*scene)->objects.size() == 37);
    // One material per sphere (25, distinct roughness/metallic) + 6 patches + the ramp + the
    // normal probe + one material shared by the three depth probes + the mip probe.
    REQUIRE((*scene)->tableStats().materialCount == 35);
    // Sphere, unit quad (shared by the patches, the normal probe, and the mip probe), gradient
    // ramp quad, cube.
    REQUIRE((*scene)->tableStats().meshCount - 1 == 4);
    // The gradient ramp, the normal map, and the mip probe's checkerboard.
    std::vector<TextureId> textures;
    for (const auto& object : (*scene)->objects) {
        const auto& material = (*scene)->material(object.material);
        for (const auto id : {material.diffuse, material.normalMap, material.metallicRoughness,
                              material.occlusion, material.emissiveMap}) {
            if (id && std::ranges::find(textures, *id) == textures.end()) {
                textures.push_back(*id);
            }
        }
    }
    REQUIRE(textures.size() == 3);

    size_t normalMapped = 0;
    for (const auto& object : (*scene)->objects) {
        const auto& material = (*scene)->material(object.material);
        if (material.normalMap.has_value()) {
            ++normalMapped;
        }
    }
    REQUIRE(normalMapped == 1);

    REQUIRE((*scene)->boundingSphere.w > 0.0f);
    REQUIRE((*scene)->skyCubemap != nullptr);

    REQUIRE(near3((*scene)->initialCamera.position, glm::vec3(0.0f, 0.0f, 12.0f)));
    REQUIRE((*scene)->initialCamera.yaw == 0.0f);
    REQUIRE((*scene)->initialCamera.pitch == 0.0f);
}

//======================================================================================================================
TEST_CASE("loadMaterialLabScene uses its neutral fallback when the studio environment is absent",
          "[gpu]") {
    const auto loadAndCheckFallback = [] {
        auto device = rojoRHI::createDevice();
        REQUIRE(device.has_value());
        auto scene = lmx::scenes::loadMaterialLabScene(**device);
        INFO(describeSceneError(scene));
        REQUIRE(scene.has_value());
        REQUIRE((*scene)->skyCubemap != nullptr);
        REQUIRE((*scene)->irradianceMap != nullptr);
        REQUIRE((*scene)->prefilteredEnvMap != nullptr);
        REQUIRE((*scene)->dfgLut != nullptr);
        REQUIRE(std::any_of(std::begin((*scene)->lights), std::end((*scene)->lights),
                            [](const lmx::engine::DirectionalLight& light) {
                                return glm::length(light.strength) > 0.0f;
                            }));
    };

    if (const auto directory = findRepoAsset("Assets/Fetched/MaterialLab")) {
        const TemporarilyHiddenDirectory hidden(*directory);
        loadAndCheckFallback();
    } else {
        loadAndCheckFallback();
    }
}

//======================================================================================================================
TEST_CASE("loadMaterialLabScene does not double-light the fetched studio environment", "[gpu]") {
    if (!findRepoAsset("Assets/Fetched/MaterialLab/studio_small_09_1k.hdr")) {
        SKIP("Studio Small 09 is not present (xmake setup fetches it)");
    }

    auto device = rojoRHI::createDevice();
    REQUIRE(device.has_value());
    auto scene = lmx::scenes::loadMaterialLabScene(**device);
    INFO(describeSceneError(scene));
    REQUIRE(scene.has_value());
    for (const lmx::engine::DirectionalLight& light : (*scene)->lights) {
        REQUIRE(near3(light.strength, glm::vec3(0.0f)));
    }
}

//======================================================================================================================
TEST_CASE("loadMaterialLabScene's sphere grid sweeps roughness across columns and metallic "
          "across rows",
          "[gpu]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device.has_value());
    auto scene = lmx::scenes::loadMaterialLabScene(**device);
    INFO(describeSceneError(scene));
    REQUIRE(scene.has_value());

    const auto materialOf = [&](std::string_view name) -> const MaterialRecord& {
        const SceneObject* object = findObject(**scene, name);
        REQUIRE(object != nullptr);
        return (*scene)->material(object->material);
    };

    REQUIRE(materialOf("material-lab sphere r0c0").roughness == Catch::Approx(0.05f));
    REQUIRE(materialOf("material-lab sphere r0c4").roughness == Catch::Approx(1.0f));
    // The two rows the furnace case probes: a pure dielectric and a pure conductor.
    REQUIRE(materialOf("material-lab sphere r0c0").metallic == Catch::Approx(0.0f));
    REQUIRE(materialOf("material-lab sphere r4c0").metallic == Catch::Approx(1.0f));
    REQUIRE(materialOf("material-lab sphere r2c2").metallic == Catch::Approx(0.5f));
    // Albedo stays white across the whole grid -- only roughness and metallic sweep.
    REQUIRE(near3(glm::vec3(materialOf("material-lab sphere r2c2").albedo), glm::vec3(1.0f)));
}

//======================================================================================================================
// A pure projection check (no rendering): panning the initial camera along +X to the depth lane
// frames every probe without changing yaw, pitch, Y, Z, or FOV. This catches probe placements where
// the far probe's screen footprint sits entirely inside the mid probe's and hides it completely.
TEST_CASE("loadMaterialLabScene's depth lane is framed by a horizontal camera pan and its probes "
          "do not occlude each other",
          "[gpu]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device.has_value());
    auto scene = lmx::scenes::loadMaterialLabScene(**device);
    INFO(describeSceneError(scene));
    REQUIRE(scene.has_value());

    lmx::engine::Camera camera;
    camera.position = (*scene)->initialCamera.position;
    camera.yaw = (*scene)->initialCamera.yaw;
    camera.pitch = (*scene)->initialCamera.pitch;
    camera.fovY = (*scene)->initialCamera.fovY;
    camera.nearZ = (*scene)->initialCamera.nearZ;
    camera.farZ = (*scene)->initialCamera.farZ;
    camera.position.x = 28.0f;

    constexpr uint32_t kSize = 256;
    const auto boxOf = [&](std::string_view name, const glm::vec3& halfExtent) {
        const SceneObject* object = findObject(**scene, name);
        REQUIRE(object != nullptr);
        return projectAabbToScreen(camera, kSize, object->position, halfExtent);
    };

    const ScreenBox nearBox = boxOf("material-lab depth probe near", glm::vec3(0.25f));
    const ScreenBox midBox = boxOf("material-lab depth probe mid", glm::vec3(0.25f));
    const ScreenBox farBox = boxOf("material-lab depth probe far", glm::vec3(0.25f));
    for (const ScreenBox& box : {nearBox, midBox, farBox}) {
        INFO("box: x[" + std::to_string(box.minX) + "," + std::to_string(box.maxX) + "] y[" +
             std::to_string(box.minY) + "," + std::to_string(box.maxY) + "]");
        REQUIRE(insideFrame(box, static_cast<float>(kSize)));
    }

    REQUIRE(disjoint(nearBox, midBox));
    REQUIRE(disjoint(nearBox, farBox));
    REQUIRE(disjoint(midBox, farBox));
}

//======================================================================================================================
// The opening view is a lookdev view, not an inventory thumbnail: the complete sphere matrix must
// be visible, yet large enough that roughness and reflection changes are immediately readable.
TEST_CASE("loadMaterialLabScene opens with the complete sphere matrix prominent", "[gpu]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device.has_value());
    auto scene = lmx::scenes::loadMaterialLabScene(**device);
    INFO(describeSceneError(scene));
    REQUIRE(scene.has_value());

    lmx::engine::Camera camera;
    camera.position = (*scene)->initialCamera.position;
    camera.yaw = (*scene)->initialCamera.yaw;
    camera.pitch = (*scene)->initialCamera.pitch;
    camera.fovY = (*scene)->initialCamera.fovY;
    camera.nearZ = (*scene)->initialCamera.nearZ;
    camera.farZ = (*scene)->initialCamera.farZ;

    constexpr uint32_t kSize = 256;
    const ScreenBox gridBox =
        projectAabbToScreen(camera, kSize, glm::vec3(0.0f), glm::vec3(3.5f, 3.5f, 0.5f));
    REQUIRE(insideFrame(gridBox, static_cast<float>(kSize)));
    REQUIRE((gridBox.maxY - gridBox.minY) / static_cast<float>(kSize) > 0.65f);
    REQUIRE((gridBox.maxY - gridBox.minY) / static_cast<float>(kSize) < 0.80f);
}

//======================================================================================================================
// The authored-colour and texture diagnostics share one framing reached solely by translating the
// initial camera along X. Exact X centres also pin the left-to-right lane ordering as scene data.
TEST_CASE("loadMaterialLabScene arranges texture diagnostics in a horizontally pannable lane",
          "[gpu]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device.has_value());
    auto scene = lmx::scenes::loadMaterialLabScene(**device);
    INFO(describeSceneError(scene));
    REQUIRE(scene.has_value());

    lmx::engine::Camera camera;
    camera.position = (*scene)->initialCamera.position;
    camera.position.x = 14.0f;
    camera.yaw = (*scene)->initialCamera.yaw;
    camera.pitch = (*scene)->initialCamera.pitch;
    camera.fovY = (*scene)->initialCamera.fovY;
    camera.nearZ = (*scene)->initialCamera.nearZ;
    camera.farZ = (*scene)->initialCamera.farZ;

    constexpr uint32_t kSize = 256;
    const ScreenBox patches = projectAabbToScreen(camera, kSize, glm::vec3(14.0f, 1.5f, 0.0f),
                                                  glm::vec3(3.5f, 0.5f, 0.0f));
    const ScreenBox ramp = projectAabbToScreen(camera, kSize, glm::vec3(14.0f, 0.0f, 0.0f),
                                               glm::vec3(3.0f, 0.5f, 0.0f));
    const ScreenBox normal = projectAabbToScreen(camera, kSize, glm::vec3(14.0f, -1.5f, 0.0f),
                                                 glm::vec3(0.5f, 0.5f, 0.0f));

    REQUIRE(insideFrame(patches, static_cast<float>(kSize)));
    REQUIRE(insideFrame(ramp, static_cast<float>(kSize)));
    REQUIRE(insideFrame(normal, static_cast<float>(kSize)));

    const SceneObject* red = findObject(**scene, "material-lab patch red");
    const SceneObject* black = findObject(**scene, "material-lab patch black");
    const SceneObject* rampObject = findObject(**scene, "material-lab gradient ramp");
    const SceneObject* normalObject = findObject(**scene, "material-lab normal probe");
    REQUIRE(red != nullptr);
    REQUIRE(black != nullptr);
    REQUIRE(rampObject != nullptr);
    REQUIRE(normalObject != nullptr);
    REQUIRE(red->position.x == Catch::Approx(11.0f));
    REQUIRE(black->position.x == Catch::Approx(17.0f));
    REQUIRE(rampObject->position.x == Catch::Approx(14.0f));
    REQUIRE(normalObject->position.x == Catch::Approx(14.0f));
}

//======================================================================================================================
// The flat region of the normal map is data (RGBA8Unorm, no lighting involved), so this copies
// the uploaded texture verbatim via Shaders/FullscreenSample.slang's `Load`-based passthrough
// rather than trying to reconstruct it from a lit render.
TEST_CASE("loadMaterialLabScene's normal-map probe encodes an exact flat {128,128,255,255} "
          "outside the bump",
          "[gpu]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device.has_value());
    auto scene = lmx::scenes::loadMaterialLabScene(**device);
    INFO(describeSceneError(scene));
    REQUIRE(scene.has_value());

    const SceneObject* normalProbe = findObject(**scene, "material-lab normal probe");
    REQUIRE(normalProbe != nullptr);
    rojoRHI::Texture* normalMap =
        (*scene)->tryTexture(*(*scene)->material(normalProbe->material).normalMap);
    REQUIRE(normalMap != nullptr);

    constexpr uint32_t kMapSize = 64;
    auto destination = (*device)->createTexture({.width = kMapSize,
                                                 .height = kMapSize,
                                                 .format = rojoRHI::Format::BGRA8Unorm,
                                                 .renderTarget = true,
                                                 .cpuReadback = true,
                                                 .label = "lmx.test.materialLabNormalCopy"});
    INFO(describeSceneError(destination));
    REQUIRE(destination.has_value());

    auto library = (*device)->loadShaderLibrary("Shaders/FullscreenSample");
    INFO(describeSceneError(library));
    REQUIRE(library.has_value());
    auto pipeline =
        (*device)->createGraphicsPipeline({.library = library->get(),
                                           .vertexEntry = "vertexMain",
                                           .fragmentEntry = "fragmentMain",
                                           .colorFormat = rojoRHI::Format::BGRA8Unorm,
                                           .label = "lmx.test.materialLabNormalCopyPipeline"});
    INFO(describeSceneError(pipeline));
    REQUIRE(pipeline.has_value());

    rojoRHI::CommandList& commands = (*device)->beginFrame();
    commands.beginRenderPass({.colorTarget = destination->get(),
                              .clearColor = {1.0f, 0.0f, 1.0f, 1.0f},
                              .clear = true,
                              .label = "lmx.test.materialLabNormalCopy"});
    commands.bindPipeline(**pipeline);
    commands.bindTexture(0, *normalMap);
    commands.draw(3);
    commands.endRenderPass();
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint8_t> pixels(size_t{kMapSize} * kMapSize * 4);
    (*destination)->readback(pixels.data(), pixels.size());

    // Texel (0,0) is well outside the centred bump radius (28 texels from the (31.5,31.5)
    // centre) -- flat. Sorted so the assertion does not depend on the destination's BGRA channel
    // order, only on the multiset of bytes {128,128,255,255} being present.
    std::array<uint8_t, 4> corner = {pixels[0], pixels[1], pixels[2], pixels[3]};
    std::sort(corner.begin(), corner.end());
    INFO("corner texel bytes (sorted): " + std::to_string(corner[0]) + "," +
         std::to_string(corner[1]) + "," + std::to_string(corner[2]) + "," +
         std::to_string(corner[3]));
    REQUIRE(corner == std::array<uint8_t, 4>{128, 128, 255, 255});
}

//======================================================================================================================
// The whole colour pipeline end to end, on the scene built to hold it still. Ambient 1.0 with
// every directional light dark and no sky bound makes the fragment's linear output exactly the
// material's albedo -- which the scene decoded from an authored sRGB constant -- so each patch's
// readback must be that constant decoded, tone mapped, and encoded, with nothing else in between.
// A second decode or a second encode anywhere on that path moves every patch off its number.
//
// Derived expectations at exposure 0, from Tests/DisplayTransformOracle.h (bytes, rounded):
//   red   (1,0,0)      -> linear (1, 0, 0)          -> 241, 33, 33
//   green (0,1,0)      -> linear (0, 1, 0)          -> 33, 241, 33
//   blue  (0,0,1)      -> linear (0, 0, 1)          -> 33, 33, 241
//   gray18 (0.46)      -> linear 0.1789             -> 104, 104, 104
//   white (1,1,1)      -> linear 1.0                -> 240, 240, 240
//   black (0,0,0)      -> linear 0.0                -> 0, 0, 0
// The saturated primaries land on 33 in their two dark channels rather than 0 because their peak
// channel is above the tone map's 0.76 shoulder, where it desaturates toward the compressed peak;
// the achromatic patches sit below it and lose only the constant 0.04 black offset.
TEST_CASE("loadMaterialLabScene's known-colour patches round-trip the display transform and its "
          "gradient ramp reads back monotonic",
          "[gpu]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device.has_value());
    auto scene = lmx::scenes::loadMaterialLabScene(**device);
    INFO(describeSceneError(scene));
    REQUIRE(scene.has_value());

    struct Patch {
        const char* name;
        glm::vec3 authoredSrgb;
    };
    const std::array<Patch, 6> kPatches = {{
        {"red", {1.0f, 0.0f, 0.0f}},
        {"green", {0.0f, 1.0f, 0.0f}},
        {"blue", {0.0f, 0.0f, 1.0f}},
        {"gray18", {0.46f, 0.46f, 0.46f}},
        {"white", {1.0f, 1.0f, 1.0f}},
        {"black", {0.0f, 0.0f, 0.0f}},
    }};

    const SceneObject* ramp = findObject(**scene, "material-lab gradient ramp");
    REQUIRE(ramp != nullptr);

    constexpr uint32_t kProbeSize = 64;
    auto renderer = render::Renderer::create(**device, kProbeSize, kProbeSize,
                                             /*cpuReadback=*/true);
    INFO(describeSceneError(renderer));
    REQUIRE(renderer.has_value());

    // A bespoke camera centred between the patch row and ramp in their horizontal texture lane.
    // Only those selected draw items are submitted, isolating the colour pipeline under test.
    lmx::engine::Camera camera;
    camera.position = {14.0f, 0.75f, 8.5f};
    camera.fovY = glm::radians(45.0f);
    camera.nearZ = 0.1f;
    camera.farZ = 20.0f;

    rojoRHI::CommandList& commands = (*device)->beginFrame();
    REQUIRE((*scene)->prepareFrame((*device)->frameNumber()).has_value());
    std::vector<lmx::engine::DrawItem> allItems;
    render::buildSceneView(**scene, allItems, render::ShadowFilter::PCF, false);
    std::vector<lmx::engine::DrawItem> items;
    std::vector<glm::vec3> patchPositions;
    for (const Patch& patch : kPatches) {
        const SceneObject* object =
            findObject(**scene, std::string("material-lab patch ") + patch.name);
        REQUIRE(object != nullptr);
        patchPositions.push_back(object->position);
        items.push_back(allItems[static_cast<size_t>(object - (*scene)->objects.data())]);
    }
    items.push_back(allItems[static_cast<size_t>(ramp - (*scene)->objects.data())]);

    render::SceneView view;
    view.items = items;
    view.tables = (*scene)->tables();
    // A white uniform environment and no analytic lights: every patch is lit only by the
    // image-based terms, which for a constant environment are that environment's own radiance
    // (Engine/Asset/Texture/Ibl.h) -- so what reaches the target is the patch's total reflectance
    // and nothing about the geometry of a light rig enters the expectation.
    const lmx::engine::ibl::IblTextures environment =
        lmx::test::makeUniformIbl(**device, glm::vec3(1.0f), "lmx.test.patchFurnace");
    view.irradiance = environment.irradiance.get();
    view.prefilteredEnv = environment.prefilteredEnv.get();
    view.dfgLut = environment.dfgLut.get();
    for (lmx::engine::DirectionalLight& light : view.lights) {
        light.strength = {0.0f, 0.0f, 0.0f};
    }
    view.boundingSphere = {14.0f, 0.75f, 0.0f, 5.0f};

    (*renderer)->render(commands, camera, view, /*barrierForSampling=*/false);
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint8_t> pixels(size_t{kProbeSize} * kProbeSize * 4);
    (*renderer)->colorTarget().readback(pixels.data(), pixels.size());

    // Readback hands back BGRA, so the channel at offset 0 is blue.
    const auto channelAt = [&](uint32_t x, uint32_t y, size_t channel) -> uint8_t {
        const size_t offset = (size_t{y} * kProbeSize + x) * 4;
        return pixels[offset + channel];
    };
    constexpr std::array<size_t, 3> kRgbOffsets = {2, 1, 0};

    // Each patch is a quad in the Z = 0 plane, so its normal is +Z and its view vector is fixed by
    // the camera's offset from it. Under a unit environment the shaded radiance is the split-sum
    // reconstruction of that surface -- a white patch returns the environment exactly (it absorbs
    // nothing), a black one returns only its 4% dielectric Fresnel share, and the coloured patches
    // land between -- so the authored colour still round-trips, now through the material model
    // instead of past it.
    for (size_t i = 0; i < kPatches.size(); ++i) {
        const glm::vec3 linear{srgbToLinear(kPatches[i].authoredSrgb)};
        const glm::vec3 toEye = glm::normalize(camera.position - patchPositions[i]);
        const lmx::test::brdf::Surface surface{.baseColor = linear};
        const glm::vec3 radiance = lmx::test::brdf::imageBasedLight(
            glm::vec3(1.0f), glm::vec3(1.0f),
            lmx::test::brdf::sampleDfg(glm::dot(glm::vec3(0.0f, 0.0f, 1.0f), toEye),
                                       surface.perceptualRoughness),
            surface);
        const std::array<int, 3> want = lmx::test::displayBytes(radiance);
        const ProjectedPixel coord = projectScenePixel(camera, kProbeSize, patchPositions[i]);
        REQUIRE(coord.x < kProbeSize);
        REQUIRE(coord.y < kProbeSize);
        for (size_t channel = 0; channel < 3; ++channel) {
            const int value = channelAt(coord.x, coord.y, kRgbOffsets[channel]);
            INFO(std::string(kPatches[i].name) + " patch channel " + std::to_string(channel) +
                 " = " + std::to_string(value) + ", expected " + std::to_string(want[channel]));
            REQUIRE(std::abs(value - want[channel]) <= 3);
        }
    }

    // Gradient ramp: byte i in all channels at texel column i, sampled left to right. The readback
    // must be non-decreasing end to end, with a wide spread -- a probe that would also pass
    // against a flat or empty ramp is not discriminating anything -- and no adjacent pair may jump
    // by more than kMaxRampStep, which is what a posterised intermediate would show up as.
    //
    // kMaxRampStep is derived, not measured: these 24 probes span authored bytes 4..251, so the
    // ideal chain moves 15.2 bytes at its steepest adjacent pair (just above the tone map's 0.08
    // knee, where the black offset's slope in encoded space peaks). 22 leaves margin for the
    // ramp's bilinear filtering and the target's own rounding without admitting a visible band.
    constexpr int kRampSamples = 24;
    constexpr int kMaxRampStep = 22;
    std::vector<int> samples;
    for (int i = 0; i < kRampSamples; ++i) {
        const float worldX =
            ramp->position.x - 2.9f + static_cast<float>(i) * (5.8f / (kRampSamples - 1));
        const glm::vec3 world{worldX, ramp->position.y, ramp->position.z};
        const ProjectedPixel coord = projectScenePixel(camera, kProbeSize, world);
        REQUIRE(coord.x < kProbeSize);
        REQUIRE(coord.y < kProbeSize);
        samples.push_back(channelAt(coord.x, coord.y, /*channel=*/0));
    }
    for (size_t i = 1; i < samples.size(); ++i) {
        INFO("ramp sample " + std::to_string(i - 1) + "=" + std::to_string(samples[i - 1]) +
             ", sample " + std::to_string(i) + "=" + std::to_string(samples[i]));
        REQUIRE(samples[i] >= samples[i - 1]);
        REQUIRE(samples[i] - samples[i - 1] <= kMaxRampStep);
    }
    INFO("ramp spread: " + std::to_string(samples.front()) + " -> " +
         std::to_string(samples.back()));
    REQUIRE(samples.back() - samples.front() > 100);
}

//======================================================================================================================
// Exit-gate check: Metal's blit generateMipmaps was measured to point-pick, not filter, so a
// point-picked mip of MaterialLab's 1-texel checkerboard (makeCheckerboardPixels) reads solid
// black or solid white -- stepping by a power of two always lands on the same parity. A correctly
// box-filtered chain (Source/Engine/Asset/Texture/TextureBake.h's bakeMips, the same function the
// offline bake tool uses) instead converges every level above 0 to an exact uniform mid-gray,
// because every 2x2 block of a 1-texel checkerboard contains exactly two black and two white
// texels.
//
// The bespoke camera sits 10 world units from the probe -- a 1x1 unit quad at that distance,
// against a 64px target and 45-degree vertical FOV, covers roughly 8 screen pixels while sampling
// a 64-texel-wide texture, comfortably selecting a mip level above 0 (texel/pixel ratio ~8, so LOD
// ~3) while still covering enough pixels that rasterization cannot miss every sample. The probe's
// own material has diffuse = the checkerboard texture (sRGB) and albedo = white, so the base colour
// the shader sees at this probe is the sampled texel: 0.5 for a correctly filtered mip.
//
// Lighting is a white uniform environment and no analytic lights -- the same configuration as this
// file's known-colour patch test above -- so the shaded radiance is that base colour's split-sum
// reconstruction, stated here through the same CPU mirror of the BRDF (Tests/BrdfOracle.h) and then
// through the full display path (Tests/DisplayTransformOracle.h, mirroring the PBR Neutral tone map
// and the sRGB encode). What matters to this case is only that the three outcomes stay well
// separated: a point-picked mip lands on the black or the white end, and a filtered one lands
// between them.
TEST_CASE("loadMaterialLabScene's mip probe converges to mid-gray under strong minification, "
          "proving its mips are filtered rather than point-picked",
          "[gpu]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device.has_value());
    auto scene = lmx::scenes::loadMaterialLabScene(**device);
    INFO(describeSceneError(scene));
    REQUIRE(scene.has_value());

    const SceneObject* probe = findObject(**scene, "material-lab mip probe");
    REQUIRE(probe != nullptr);

    constexpr uint32_t kProbeSize = 64;
    auto renderer = render::Renderer::create(**device, kProbeSize, kProbeSize,
                                             /*cpuReadback=*/true);
    INFO(describeSceneError(renderer));
    REQUIRE(renderer.has_value());

    // 10 world units back from the probe, on the +Z side its quad normal faces (the probe's front
    // face is invisible from initialCamera on the other side by construction -- see
    // MaterialLab.cpp's file-level comment).
    lmx::engine::Camera camera;
    camera.position = probe->position + glm::vec3(0.0f, 0.0f, 10.0f);
    camera.fovY = glm::radians(45.0f);
    camera.nearZ = 0.1f;
    camera.farZ = 20.0f;

    rojoRHI::CommandList& commands = (*device)->beginFrame();
    REQUIRE((*scene)->prepareFrame((*device)->frameNumber()).has_value());
    std::vector<lmx::engine::DrawItem> allItems;
    render::buildSceneView(**scene, allItems, render::ShadowFilter::PCF, false);
    std::vector<lmx::engine::DrawItem> items;
    items.push_back(allItems[static_cast<size_t>(probe - (*scene)->objects.data())]);

    render::SceneView view;
    view.items = items;
    view.tables = (*scene)->tables();
    const lmx::engine::ibl::IblTextures environment =
        lmx::test::makeUniformIbl(**device, glm::vec3(1.0f), "lmx.test.mipProbeFurnace");
    view.irradiance = environment.irradiance.get();
    view.prefilteredEnv = environment.prefilteredEnv.get();
    view.dfgLut = environment.dfgLut.get();
    for (lmx::engine::DirectionalLight& light : view.lights) {
        light.strength = {0.0f, 0.0f, 0.0f};
    }
    view.boundingSphere = {probe->position.x, probe->position.y, probe->position.z, 2.0f};

    (*renderer)->render(commands, camera, view, /*barrierForSampling=*/false);
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint8_t> pixels(size_t{kProbeSize} * kProbeSize * 4);
    (*renderer)->colorTarget().readback(pixels.data(), pixels.size());

    const ProjectedPixel coord = projectScenePixel(camera, kProbeSize, probe->position);
    REQUIRE(coord.x < kProbeSize);
    REQUIRE(coord.y < kProbeSize);
    const size_t offset = (static_cast<size_t>(coord.y) * kProbeSize + coord.x) * 4;

    // Point-picking this pattern reads solid black or solid white as the base colour. Each of the
    // three candidates is pushed through the same BRDF and the same display path, so the comparison
    // below is between what the shader would produce in each case rather than between raw texels.
    // The probe quad faces the camera head-on, so N.V is 1.
    const auto displayByteOfBaseColor = [](float baseColor) {
        const lmx::test::brdf::Surface surface{.baseColor = glm::vec3(baseColor)};
        const glm::vec3 radiance = lmx::test::brdf::imageBasedLight(
            glm::vec3(1.0f), glm::vec3(1.0f),
            lmx::test::brdf::sampleDfg(1.0f, surface.perceptualRoughness), surface);
        return lmx::test::displayBytes(radiance)[0];
    };
    const int expected = displayByteOfBaseColor(0.5f);
    const int blackExtreme = displayByteOfBaseColor(0.0f);
    const int whiteExtreme = displayByteOfBaseColor(1.0f);

    for (size_t channel = 0; channel < 3; ++channel) {
        const int value = pixels[offset + channel];
        INFO("mip probe channel " + std::to_string(channel) + " = " + std::to_string(value) +
             ", expected " + std::to_string(expected));
        REQUIRE(std::abs(value - expected) <= 3);
        REQUIRE(value > blackExtreme + 40);
        REQUIRE(value < whiteExtreme - 40);
    }
}
