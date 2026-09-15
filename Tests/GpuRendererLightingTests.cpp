#include "GpuRendererTestSupport.h"
#include "SceneTableTestSupport.h"

using lmx::test::FixtureDrawItem;
using lmx::test::FixtureMaterial;
using lmx::test::FixtureMesh;
using lmx::test::fixtureMesh;
using lmx::test::FixtureSceneView;

//======================================================================================================================
// The exit gate on the reversed projection: not that its matrix has the entries it should, which
// Tests/RenderTests.cpp pins, but that the number a real frame leaves in the depth buffer inverts
// back to the distance the geometry actually sits at.
//
// The projection emits clip.z = nearZ and clip.w = -z_view, so a fragment stores
// d = nearZ / (-z_view) and the inverse is
//
//     z_view = -nearZ / d
//
// with no far plane anywhere in it -- which is the claim being tested, since a conventional or
// finite-far projection would need farZ to invert and would land somewhere else at every probe.
//
// MaterialLab supplies the geometry: three cubes on the camera's forward axis at documented
// distances, so the reference is arithmetic on numbers the scene wrote down rather than a second
// measurement. Each probe's visible surface is its front face, one half-extent nearer than its
// centre, and that face is perpendicular to the view axis -- so every pixel on it holds the same
// depth and the reconstruction has no interpolation error to absorb.
//
// Tolerance: 2e-3 relative, roughly twice the worst case of the one lossy step. The depth itself
// is D32Float (relative error ~6e-8, and depth is linear in screen space so the rasterizer's
// interpolation across a plane is exact), but the probe pass has to carry it out through an
// RGBA16Float target -- the only float format this RHI renders into, and D32Float has no packed
// readback of its own. Binary16 keeps 11 significant bits, and the measured probes here all land
// one binary16 ulp low rather than at the nearest value, so the bound is a whole ulp: at most
// 2^-10 = 9.8e-4 relative. Measured at the three probes: 2.4e-4, 2.4e-4, 7.3e-4. The
// reconstruction divides by d, which carries relative error through unchanged rather than
// amplifying it, so that bound is the answer's bound too.
TEST_CASE("view depth reconstructs from the scene depth buffer at MaterialLab's probes", "[gpu]") {
    using namespace lmx::rhi;

    constexpr uint32_t kSourceTextureSlot = 0;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto scene = lmx::scene::loadMaterialLabScene(**device);
    REQUIRE(scene.has_value());

    auto renderer = Renderer::create(**device, kDepthReconstructSize, kDepthReconstructSize);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    Camera camera;
    camera.position = (*scene)->initialCamera.position;
    camera.yaw = (*scene)->initialCamera.yaw;
    camera.pitch = (*scene)->initialCamera.pitch;
    camera.fovY = (*scene)->initialCamera.fovY;
    camera.nearZ = (*scene)->initialCamera.nearZ;
    camera.farZ = (*scene)->initialCamera.farZ;
    REQUIRE(camera.position.z == kMaterialLabCameraDistance);
    camera.position.x = kMaterialLabDepthLaneX;

    // The depth buffer is D32Float, which readback() has no packed texel size for, so the probe
    // pass copies it into a half-float target that does.
    auto probeImage = (*device)->createTexture({.width = kDepthReconstructSize,
                                                .height = kDepthReconstructSize,
                                                .format = Format::RGBA16Float,
                                                .renderTarget = true,
                                                .cpuReadback = true,
                                                .label = "lmx.test.depthProbeImage"});
    INFO(errorOf(probeImage));
    REQUIRE(probeImage.has_value());

    auto probeLibrary = (*device)->loadShaderLibrary("Shaders/FullscreenSample");
    INFO(errorOf(probeLibrary));
    REQUIRE(probeLibrary.has_value());

    auto probePipeline =
        (*device)->createGraphicsPipeline({.library = probeLibrary->get(),
                                           .vertexEntry = "vertexMain",
                                           .fragmentEntry = "fragmentMain",
                                           .colorFormat = Format::RGBA16Float,
                                           .cullMode = CullMode::None,
                                           .label = "lmx.test.depthProbePipeline"});
    INFO(errorOf(probePipeline));
    REQUIRE(probePipeline.has_value());

    CommandList& commands = (*device)->beginFrame();
    REQUIRE((*scene)->prepareFrame((*device)->frameNumber()));
    std::vector<lmx::render::DrawItem> items;
    const auto view = (*scene)->view(items, lmx::render::ShadowFilter::PCF, false);
    (*renderer)->render(commands, camera, lmx::test::prepareSceneView(view, device),
                        /*barrierForSampling=*/false);
    commands.textureBarrier((*renderer)->depthTarget(), TextureUse::RenderTarget,
                            TextureUse::ShaderRead);
    commands.beginRenderPass({.colorTarget = probeImage->get(),
                              .clearColor = {0.0f, 0.0f, 0.0f, 1.0f},
                              .clear = true,
                              .label = "lmx.test.depthProbe.copy"});
    commands.bindPipeline(**probePipeline);
    commands.bindTexture(kSourceTextureSlot, (*renderer)->depthTarget());
    commands.draw(3);
    commands.endRenderPass();
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint16_t> texels(size_t{kDepthReconstructSize} * kDepthReconstructSize * 4);
    (*probeImage)->readback(texels.data(), texels.size() * sizeof(uint16_t));

    const glm::mat4 viewProj = camera.projectionMatrix(1.0f) * camera.viewMatrix();
    for (const DepthProbe& probe : kMaterialLabDepthProbes) {
        INFO(std::string("probe: ") + probe.name);

        // The point the camera actually sees: the centre of the cube's near face.
        const float faceDistance = probe.distance - kDepthProbeHalfExtent;
        const glm::vec3 world{kMaterialLabDepthLaneX + probe.lateralOffset, 0.0f,
                              kMaterialLabCameraDistance - probe.distance + kDepthProbeHalfExtent};

        const glm::vec4 clip = viewProj * glm::vec4(world, 1.0f);
        REQUIRE(clip.w > 0.0f);
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        INFO("ndc x " + std::to_string(ndc.x) + " y " + std::to_string(ndc.y));
        REQUIRE(std::abs(ndc.x) < 1.0f);
        REQUIRE(std::abs(ndc.y) < 1.0f);
        const auto x = static_cast<uint32_t>((ndc.x * 0.5f + 0.5f) *
                                             static_cast<float>(kDepthReconstructSize));
        const auto y = static_cast<uint32_t>((0.5f - ndc.y * 0.5f) *
                                             static_cast<float>(kDepthReconstructSize));

        const size_t offset = (size_t{y} * kDepthReconstructSize + x) * 4;
        const float sampled = floatOfHalfBits(texels[offset]);
        INFO("sampled depth " + std::to_string(sampled) + " at pixel " + std::to_string(x) + "," +
             std::to_string(y));
        // A cleared texel would be 0, which the reconstruction cannot divide by -- and would mean
        // the probe pixel found sky rather than the cube.
        REQUIRE(sampled > 0.0f);

        const float reconstructed = -camera.nearZ / sampled;
        INFO("reconstructed z_view " + std::to_string(reconstructed) + ", reference " +
             std::to_string(-faceDistance));
        REQUIRE(reconstructed == Catch::Approx(-faceDistance).epsilon(2e-3));
    }
}

//======================================================================================================================
// Exit gate: the white furnace. A surface with albedo 1 in a uniform environment of radiance E has
// to return exactly E -- it absorbs nothing, so every photon that arrived leaves again -- and that
// is true whatever its roughness and whichever way it is facing.
//
// The environment is built through the production generators on a constant white cube, so this
// exercises the same irradiance convolution, prefiltered chain and RG16Float DFG table a real scene
// carries; both generators reproduce a constant environment exactly at every roughness
// (Source/Asset/Ibl.h, pinned in Tests/EngineIblTests.cpp), so the expected reading is 1.0 with no
// integration error folded into it. Lights are off and exposure is 0, which leaves the image-based
// terms as the entire signal, and the HDR target is read directly so no tone map stands in the way.
//
// The bound is derived from the quantization chain rather than chosen. E = 1.0 is exact in binary16
// and survives the cube upload unchanged; the DFG table's own half-precision error cancels
// algebraically, because the diffuse term is defined as the energy the two specular terms did not
// take and every one of the three reads the same (scale, bias) pair; the shader itself computes in
// float32. That leaves the RGBA16Float scene target's single rounding, whose spacing at 1.0 is
// 2^-10. Four of those is 0.0039 -- the bound below, and roughly an eighth of the milestone gate's
// +/- 0.03 window.
//
// Only the dielectric and conductor rows are held to that equality. The rows between them are a
// blend of two materials rather than a material, so no furnace closes there (see
// Tests/RenderTests.cpp's furnace case); what they must still honour is that nothing creates
// energy, which is asserted across the whole grid.
TEST_CASE("MaterialLab's sphere grid conserves energy in a white furnace", "[gpu]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto scene = lmx::scene::loadMaterialLabScene(**device);
    INFO(errorOf(scene));
    REQUIRE(scene.has_value());

    auto renderer = Renderer::create(**device, kBrdfProbeSize, kBrdfProbeSize,
                                     /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const lmx::scene::ibl::IblTextures environment =
        lmx::test::makeUniformIbl(**device, glm::vec3(1.0f), "lmx.test.whiteFurnace");

    // Only the sphere grid: the patches, ramp and depth probes sit outside this frustum anyway, and
    // leaving them out keeps every drawn pixel one of the 25 materials under test.
    CommandList& commands = (*device)->beginFrame();
    REQUIRE((*scene)->prepareFrame((*device)->frameNumber()));
    std::vector<lmx::render::DrawItem> allItems;
    const auto sceneView = (*scene)->view(allItems, lmx::render::ShadowFilter::PCF, false);
    std::vector<lmx::render::DrawItem> items;
    std::vector<glm::vec3> centers;
    std::vector<const lmx::scene::SceneObject*> spheres;
    for (const lmx::scene::SceneObject& object : (*scene)->objects) {
        if (!object.name.starts_with("material-lab sphere ")) {
            continue;
        }
        spheres.push_back(&object);
        centers.push_back(object.position);
        const size_t index = static_cast<size_t>(&object - (*scene)->objects.data());
        items.push_back(allItems[index]);
    }
    REQUIRE(items.size() == 25);

    lmx::render::SceneView view;
    view.items = items;
    view.tables = sceneView.tables;
    for (DirectionalLight& light : view.lights) {
        light.strength = {0.0f, 0.0f, 0.0f};
    }
    view.irradiance = environment.irradiance.get();
    view.prefilteredEnv = environment.prefilteredEnv.get();
    view.dfgLut = environment.dfgLut.get();
    view.boundingSphere = {0.0f, 0.0f, 0.0f, 6.0f};
    REQUIRE(view.exposureEv == 0.0f);

    // Close enough that each unit-diameter sphere covers around 26 pixels, so a probe at a
    // projected centre lands well inside one.
    Camera camera;
    camera.position = {0.0f, 0.0f, 12.0f};

    (*renderer)->render(commands, camera, lmx::test::prepareSceneView(view, device),
                        /*barrierForSampling=*/false);
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint16_t> texels(size_t{kBrdfProbeSize} * kBrdfProbeSize * 4);
    (*renderer)->hdrColorTarget().readback(texels.data(), texels.size() * sizeof(uint16_t));

    // Four RGBA16Float roundings at 1.0; see this case's derivation above.
    constexpr float kFurnaceTolerance = 4.0f / 1024.0f;

    for (size_t i = 0; i < spheres.size(); ++i) {
        const PixelCoord at = projectToPixel(camera, kBrdfProbeSize, centers[i]);
        const glm::vec3 radiance = hdrTexelAt(texels, kBrdfProbeSize, at.x, at.y);
        const auto& material = (*scene)->material(spheres[i]->material);
        INFO(spheres[i]->name << ": roughness " << material.roughness << ", metallic "
                              << material.metallic << ", read (" << radiance.r << ", " << radiance.g
                              << ", " << radiance.b << ")");

        // Nothing anywhere on the grid may reflect more than it received.
        REQUIRE(radiance.r <= 1.0f + kFurnaceTolerance);
        REQUIRE(radiance.g <= 1.0f + kFurnaceTolerance);
        REQUIRE(radiance.b <= 1.0f + kFurnaceTolerance);

        const bool pureDielectric = material.metallic == 0.0f;
        const bool pureConductor = material.metallic == 1.0f;
        if (pureDielectric || pureConductor) {
            REQUIRE(radiance.r == Catch::Approx(1.0f).margin(kFurnaceTolerance));
            REQUIRE(radiance.g == Catch::Approx(1.0f).margin(kFurnaceTolerance));
            REQUIRE(radiance.b == Catch::Approx(1.0f).margin(kFurnaceTolerance));
        }
    }
}

//======================================================================================================================
// Exit gate: dielectric and conductor probes against a CPU evaluation of the same BRDF at pinned
// angles.
//
// A quad rotated about X gives an exactly known shading normal, and a distant narrow-FOV camera on
// the +Z axis gives an exactly known view vector at the quad's centre, so each case below names its
// N.V and N.L rather than approximating them off a sphere's silhouette. No environment is bound, so
// the probe measures the analytic lobe alone -- the image-based half of the model is what the
// furnace case above measures, and separating them means a failure here names which one broke.
//
// The reference is Tests/BrdfOracle.h, a CPU mirror written from the same published formulations
// rather than transliterated from the shader.
TEST_CASE("dielectric and conductor probes match a CPU BRDF reference at pinned angles", "[gpu]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto quad =
        lmx::test::fixtureMesh(**device, lmx::render::makePlane(8.0f), "lmx.test.brdfProbeQuad");
    INFO(errorOf(quad));
    REQUIRE(quad.has_value());

    auto renderer = Renderer::create(**device, kBrdfProbeSize, kBrdfProbeSize,
                                     /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    struct Case {
        const char* name;
        float viewAngleDegrees; // angle between the surface normal and the view axis
        glm::vec3 lightDirection;
        glm::vec3 baseColor;
        float roughness;
        float metallic;
    };
    // Two pinned working points, then the parameter limits the shader's clamps exist for.
    const Case cases[] = {
        {"facing dielectric", 0.0f, {0.0f, -0.6f, -0.8f}, {0.8f, 0.8f, 0.8f}, 0.3f, 0.0f},
        {"oblique conductor", 40.0f, {0.0f, 0.0f, -1.0f}, {0.95f, 0.64f, 0.54f}, 0.2f, 1.0f},
        {"rough dielectric", 25.0f, {0.0f, -0.7071f, -0.7071f}, {0.5f, 0.2f, 0.1f}, 1.0f, 0.0f},
        {"roughness at the floor", 20.0f, {0.0f, -0.5f, -0.866f}, {0.9f, 0.9f, 0.9f}, 0.0f, 0.0f},
        {"rough conductor", 15.0f, {0.0f, -0.3f, -0.954f}, {1.0f, 1.0f, 1.0f}, 1.0f, 1.0f},
        {"grazing view", 80.0f, {0.0f, -0.9f, -0.436f}, {0.8f, 0.8f, 0.8f}, 0.4f, 0.0f},
    };

    const Camera camera = pinnedAngleCamera();
    constexpr glm::vec3 kLightStrength{2.0f, 2.0f, 2.0f};

    for (const Case& probe : cases) {
        INFO(probe.name);

        // Rotating the +Y plane about X by (90 - viewAngle) tilts its normal that many degrees off
        // the +Z view axis, so N.V is the cosine of the case's own angle by construction.
        const float rotation = glm::radians(90.0f - probe.viewAngleDegrees);
        const glm::mat4 model = glm::rotate(glm::mat4{1.0f}, rotation, glm::vec3{1.0f, 0.0f, 0.0f});
        const glm::vec3 normal =
            glm::normalize(glm::vec3(model * glm::vec4{0.0f, 1.0f, 0.0f, 0.0f}));
        const glm::vec3 toEye = glm::vec3{0.0f, 0.0f, 1.0f};
        REQUIRE(glm::dot(normal, toEye) ==
                Catch::Approx(std::cos(glm::radians(probe.viewAngleDegrees))).margin(1e-5));

        const std::array<FixtureDrawItem, 1> items = {{
            {.mesh = &*quad,
             .model = model,
             .material = {.albedo = glm::vec4(probe.baseColor, 1.0f),
                          .roughness = probe.roughness,
                          .metallic = probe.metallic}},
        }};

        FixtureSceneView view;
        view.items = items;
        for (DirectionalLight& light : view.lights) {
            light.strength = {0.0f, 0.0f, 0.0f};
        }
        // Light 1 rather than light 0: light 0 is the shadow caster, and a plane that shadows
        // itself at the bias limit would put the shadow filter into a BRDF measurement.
        view.lights[1] = {.strength = kLightStrength,
                          .direction = glm::normalize(probe.lightDirection)};
        view.boundingSphere = {0.0f, 0.0f, 0.0f, 12.0f};

        CommandList& commands = (*device)->beginFrame();
        (*renderer)->render(commands, camera, lmx::test::prepareSceneView(view, device),
                            /*barrierForSampling=*/false);
        (*device)->endFrame(nullptr);
        (*device)->waitIdle();

        std::vector<uint16_t> texels(size_t{kBrdfProbeSize} * kBrdfProbeSize * 4);
        (*renderer)->hdrColorTarget().readback(texels.data(), texels.size() * sizeof(uint16_t));
        const glm::vec3 radiance =
            hdrTexelAt(texels, kBrdfProbeSize, kBrdfProbeSize / 2, kBrdfProbeSize / 2);

        const glm::vec3 expected = lmx::test::brdf::directionalLight(
            kLightStrength, view.lights[1].direction, normal, toEye,
            {.baseColor = probe.baseColor,
             .perceptualRoughness = probe.roughness,
             .metallic = probe.metallic});
        INFO("N.L " << glm::dot(normal, -glm::normalize(probe.lightDirection)) << ", expected ("
                    << expected.r << ", " << expected.g << ", " << expected.b << "), read ("
                    << radiance.r << ", " << radiance.g << ", " << radiance.b << ")");

        // Relative, because these span two orders of magnitude between a rough diffuse lobe and a
        // near-mirror highlight. 1% covers the RGBA16Float target's ~0.05% storage step, the
        // half-pixel view-vector drift the camera above bounds at 0.0007 radians, and the vertex
        // interpolation of a normal the rasterizer reconstructs per fragment.
        REQUIRE(radiance.r == Catch::Approx(expected.r).epsilon(0.01).margin(1e-4));
        REQUIRE(radiance.g == Catch::Approx(expected.g).epsilon(0.01).margin(1e-4));
        REQUIRE(radiance.b == Catch::Approx(expected.b).epsilon(0.01).margin(1e-4));
    }
}
