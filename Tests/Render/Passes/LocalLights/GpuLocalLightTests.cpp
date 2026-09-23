#include "Support/GpuTestSupport.h"

#include "Engine/Lights/LocalLight.h"
#include "Engine/Lights/LocalLightMath.h"
#include "Engine/Scene/SceneTables.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <string>
#include <vector>

namespace {

using namespace lmx;

// Mirrors Shaders/Tests/PunctualLightOracle.slang's PunctualSample. Each glm::vec3 is followed by a
// scalar so both sides see the same 16-byte-aligned structured-buffer stride.
struct PunctualSample {
    engine::LightRow light;
    glm::vec3 position{0.0f};
    float pad0 = 0.0f;
    glm::vec3 normal{0.0f};
    float pad1 = 0.0f;
    glm::vec3 toEye{0.0f};
    float pad2 = 0.0f;
    glm::vec3 baseColour{0.0f};
    float metallic = 0.0f;
    glm::vec3 f0{0.0f};
    float alpha = 0.0f;
};
static_assert(sizeof(PunctualSample) == 144);
static_assert(offsetof(PunctualSample, position) == 64);
static_assert(offsetof(PunctualSample, normal) == 80);
static_assert(offsetof(PunctualSample, toEye) == 96);
static_assert(offsetof(PunctualSample, baseColour) == 112);
static_assert(offsetof(PunctualSample, f0) == 128);

// How one sample is allowed to differ from the CPU mirror. The three kinds are not a convenience:
// they record which zeros the light model actually promises, and a case declares its kind here
// rather than inferring one from the reference value it happens to produce.
enum class Agreement {
    Tolerance,     ///< Within kRelativeTolerance of the mirror, with kAbsoluteFloor under it.
    ExactZero,     ///< Bit-exact zero on both sides, from a comparison or a clearly negative cone.
    ConeEdgeBound, ///< Negligible against the same configuration at the inner cone edge.
};

// The agreement bar. The oracle shader compiles under the scene pass's own math mode, so the two
// evaluations differ by single-operation rounding and by whatever multiply-adds the GPU fuses; the
// tolerance is relative to the larger of the two magnitudes, with an absolute floor so a channel
// whose reference is vanishingly small is not held to a ratio that carries no information.
constexpr float kRelativeTolerance = 1e-5f;
constexpr float kAbsoluteFloor = 1e-6f;

// What "negligible" means at exactly the outer cone edge. The cone term is
// `saturate(cosTheta * spotScale + spotOffset)`, and `spotOffset` is `-cosOuter * spotScale`
// rounded once on the CPU, so the expression cancels to exactly zero only when the two products
// round the same way. The scene pass keeps fast math, which lets Metal contract that multiply-add
// into an FMA and compute the unrounded product instead, leaving a residue of a few ULP where the
// mirror -- built without contraction -- lands on zero. Fighting that with a math mode would make
// the oracle measure a shader the renderer does not run, and changing the cone formula is out of
// scope, so the edge is held to a ratio instead: the leak must be at most a millionth of what the
// very same light, surface and material return at the *inner* edge, where the cone term is one.
// A cone that stopped falling off, or one that fell off in the wrong place, breaks this by orders
// of magnitude; a last-ULP disagreement about where exact zero begins does not.
constexpr float kConeEdgeLeakRatio = 1e-6f;

constexpr float kDegrees = std::numbers::pi_v<float> / 180.0f;

// A material as the scene pass would have resolved it before shading.
struct Material {
    std::string name;
    glm::vec3 baseColour{1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
};

// Where a surface is and which way it faces the light and the eye.
struct Surface {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f};
    glm::vec3 toEye{0.0f};
};

// One geometric configuration of the table. The light row travels with it, so a failing sample is
// reproducible from this file alone. `innerEdge` is the companion surface a `ConeEdgeBound` case
// scales its leak against, and is unused otherwise.
struct Geometry {
    std::string name;
    engine::LightRow light;
    Surface surface;
    Agreement agreement = Agreement::Tolerance;
    Surface innerEdge;
};

//======================================================================================================================
// A unit vector perpendicular to `axis`, in the plane `axis` and `hint` span.
glm::vec3 perpendicular(glm::vec3 axis, glm::vec3 hint) {
    const glm::vec3 tangent = hint - glm::dot(hint, axis) * axis;
    REQUIRE(glm::length(tangent) > 1e-2f);
    return glm::normalize(tangent);
}

//======================================================================================================================
// The unit vector at angle `acos(cosine)` from `axis`, leaning towards `hint`. Naming the cosine
// rather than the vector is what lets a case say "grazing" or "back-facing" and mean it exactly.
glm::vec3 atCosine(glm::vec3 axis, float cosine, glm::vec3 hint) {
    const float sine = std::sqrt(std::max(1.0f - cosine * cosine, 0.0f));
    return glm::normalize(cosine * axis + sine * perpendicular(axis, hint));
}

//======================================================================================================================
engine::LightRow rowOf(const engine::LocalLight& light) {
    auto row = engine::makeLightRow(light);
    INFO(errorOf(row));
    REQUIRE(row);
    return *row;
}

//======================================================================================================================
// Builds a surface from the quantities the light model actually branches on: where it sits, how
// steeply the light strikes it (`nol`) and how steeply the eye sees it (`nov`). The two hints are
// deliberately different axes so the half vector never collapses onto the normal, which would put
// D_GGX's `noh^2 (a^2 - 1) + 1` into catastrophic cancellation at the roughness floor and make the
// comparison a measurement of that cancellation rather than of the light model.
Surface surfaceAt(const engine::LightRow& light, glm::vec3 position, float nol, float nov) {
    const glm::vec3 lightVec = glm::normalize(light.position - position);
    const glm::vec3 normal = atCosine(lightVec, nol, glm::vec3(1.0f, 0.0f, 0.0f));
    return {.position = position,
            .normal = normal,
            .toEye = atCosine(normal, nov, glm::vec3(0.0f, 0.0f, 1.0f))};
}

//======================================================================================================================
// A point `distance` metres from a spot light, placed so the light's cone term -- the value
// `SpotTerm` saturates and squares -- is exactly `term` before saturation. Naming the term rather
// than an angle is what lets the inner edge (term 1), the outer edge (term 0) and either side of
// both be stated without re-deriving the encoding here.
glm::vec3 spotPoint(const engine::LightRow& light, float term, float distance, glm::vec3 hint) {
    const float cosTheta = std::clamp((term - light.spotOffset) / light.spotScale, -1.0f, 1.0f);
    const float sinTheta = std::sqrt(std::max(1.0f - cosTheta * cosTheta, 0.0f));
    return light.position + distance * (cosTheta * light.direction +
                                        sinTheta * perpendicular(light.direction, hint));
}

//======================================================================================================================
// The sixteen geometric configurations of the oracle table, one row of the four-material cross
// product each: both light types, a surface inside, exactly at and beyond the range, both cone
// edges and well outside the cone, a back-facing normal, and grazing light and view.
std::vector<Geometry> oracleGeometry() {
    using enum engine::LocalLightType;
    const glm::vec3 down{0.0f, -1.0f, 0.0f};

    const engine::LightRow shortRange = rowOf({.type = Point,
                                               .position = {0.0f, 4.0f, 0.0f},
                                               .colour = {1.0f, 0.85f, 0.7f},
                                               .intensity = 12.0f,
                                               .range = 4.0f});
    const engine::LightRow longRange = rowOf({.type = Point,
                                              .position = {0.0f, 4.0f, 0.0f},
                                              .colour = {0.6f, 0.8f, 1.0f},
                                              .intensity = 30.0f,
                                              .range = 12.0f});
    const engine::LightRow narrow = rowOf({.type = Spot,
                                           .position = {0.0f, 4.0f, 0.0f},
                                           .colour = {0.9f, 0.95f, 1.0f},
                                           .intensity = 40.0f,
                                           .range = 10.0f,
                                           .direction = down,
                                           .innerCone = 20.0f * kDegrees,
                                           .outerCone = 40.0f * kDegrees});
    const engine::LightRow wide = rowOf({.type = Spot,
                                         .position = {2.0f, 5.0f, -1.0f},
                                         .colour = {1.0f, 0.6f, 0.4f},
                                         .intensity = 60.0f,
                                         .range = 16.0f,
                                         .direction = glm::normalize(glm::vec3(-1.0f, -2.0f, 0.5f)),
                                         .innerCone = 55.0f * kDegrees,
                                         .outerCone = 80.0f * kDegrees});

    // Straight down from a light, so the distance is exactly the length the square root returns and
    // "exactly at range" means exactly that on both sides of the comparison.
    const auto below = [&](const engine::LightRow& light, float distance) {
        return light.position + down * distance;
    };
    const glm::vec3 sideways{1.0f, 0.0f, 0.0f};
    const glm::vec3 acrossWide{0.0f, 0.0f, 1.0f};

    // The outer-edge case and the inner-edge companion its leak is measured against differ only in
    // the cone angle: same light, same distance, same incidence, same view.
    constexpr float kEdgeDistance = 4.0f;
    constexpr float kEdgeNol = 0.8f;
    constexpr float kEdgeNov = 0.5f;

    return {
        {.name = "point at exactly range",
         .light = shortRange,
         .surface = surfaceAt(shortRange, below(shortRange, 4.0f), 0.9f, 0.8f),
         .agreement = Agreement::ExactZero},
        {.name = "point well inside range",
         .light = shortRange,
         .surface = surfaceAt(shortRange, below(shortRange, 2.0f), 1.0f, 0.55f)},
        {.name = "point beyond range",
         .light = shortRange,
         .surface = surfaceAt(shortRange, below(shortRange, 6.0f), 0.9f, 0.8f),
         .agreement = Agreement::ExactZero},
        {.name = "point inside the distance floor",
         .light = shortRange,
         .surface = surfaceAt(shortRange, below(shortRange, 0.005f), 0.95f, 0.7f)},
        {.name = "point just inside range",
         .light = shortRange,
         .surface = surfaceAt(shortRange, below(shortRange, 3.9f), 0.75f, 0.6f)},
        {.name = "point off axis",
         .light = longRange,
         .surface =
             surfaceAt(longRange, longRange.position + glm::vec3(3.0f, -4.0f, 0.0f), 0.85f, 0.45f)},
        {.name = "point on a back-facing normal",
         .light = longRange,
         .surface = surfaceAt(longRange, below(longRange, 4.0f), -0.4f, 0.8f),
         .agreement = Agreement::ExactZero},
        {.name = "point at a grazing normal",
         .light = longRange,
         .surface = surfaceAt(longRange, below(longRange, 6.0f), 0.08f, 0.5f)},
        {.name = "point at a grazing view",
         .light = longRange,
         .surface = surfaceAt(longRange, below(longRange, 3.0f), 0.9f, 0.002f)},
        {.name = "spot at exactly range",
         .light = narrow,
         .surface = surfaceAt(narrow, below(narrow, 10.0f), 0.9f, 0.6f),
         .agreement = Agreement::ExactZero},
        {.name = "spot inside the inner cone",
         .light = narrow,
         .surface = surfaceAt(narrow, spotPoint(narrow, 1.25f, 3.0f, sideways), 0.95f, 0.6f)},
        {.name = "spot just outside the inner cone",
         .light = narrow,
         .surface = surfaceAt(narrow, spotPoint(narrow, 0.9f, 3.5f, sideways), 0.9f, 0.65f)},
        {.name = "spot inside the outer cone",
         .light = narrow,
         .surface = surfaceAt(narrow, spotPoint(narrow, 0.25f, 4.0f, sideways), 0.8f, 0.5f)},
        {.name = "spot at exactly the outer cone edge",
         .light = narrow,
         .surface = surfaceAt(narrow, spotPoint(narrow, 0.0f, kEdgeDistance, sideways), kEdgeNol,
                              kEdgeNov),
         .agreement = Agreement::ConeEdgeBound,
         .innerEdge = surfaceAt(narrow, spotPoint(narrow, 1.0f, kEdgeDistance, sideways), kEdgeNol,
                                kEdgeNov)},
        {.name = "spot well outside the outer cone",
         .light = narrow,
         .surface = surfaceAt(narrow, spotPoint(narrow, -0.5f, 4.0f, sideways), 0.8f, 0.5f),
         .agreement = Agreement::ExactZero},
        {.name = "wide spot inside the falloff",
         .light = wide,
         .surface = surfaceAt(wide, spotPoint(wide, 0.6f, 8.0f, acrossWide), 0.7f, 0.4f)},
    };
}

//======================================================================================================================
std::vector<Material> oracleMaterials() {
    return {
        {.name = "rough dielectric",
         .baseColour = {0.82f, 0.71f, 0.60f},
         .metallic = 0.0f,
         .roughness = 0.5f},
        {.name = "smooth metal",
         .baseColour = {0.95f, 0.78f, 0.42f},
         .metallic = 1.0f,
         .roughness = 0.2f},
        {.name = "roughness floor",
         .baseColour = {0.18f, 0.36f, 0.92f},
         .metallic = 0.0f,
         .roughness = 0.045f},
        {.name = "half metal",
         .baseColour = {0.90f, 0.90f, 0.90f},
         .metallic = 0.5f,
         .roughness = 0.85f},
    };
}

//======================================================================================================================
PunctualSample sampleOf(const engine::LightRow& light, const Surface& surface,
                        const Material& material) {
    const float roughness = std::clamp(material.roughness, 0.045f, 1.0f);
    return {.light = light,
            .position = surface.position,
            .normal = surface.normal,
            .toEye = surface.toEye,
            .baseColour = material.baseColour,
            .metallic = material.metallic,
            .f0 = glm::mix(glm::vec3(0.04f), material.baseColour, material.metallic),
            .alpha = roughness * roughness};
}

//======================================================================================================================
glm::vec3 mirrorOf(const PunctualSample& sample) {
    return engine::computePunctualLight(sample.light, sample.position, sample.normal, sample.toEye,
                                        sample.baseColour, sample.f0, sample.metallic,
                                        sample.alpha);
}

} // namespace

//======================================================================================================================
// Shaders/Common/Lighting.slang's ComputePunctualLight and
// Source/Engine/Lights/LocalLightMath.cpp's computePunctualLight are one light model written twice;
// every clustered-shading claim downstream rests on them staying one. The table below is fixed in
// this file rather than sampled randomly, so a failure names the configuration -- a range boundary,
// a cone edge, a back-facing normal -- and reruns identically.
TEST_CASE("the punctual light shader matches its CPU mirror over a fixed sample table",
          "[gpu][light]") {
    using namespace lmx;

    const std::vector<Geometry> geometry = oracleGeometry();
    const std::vector<Material> materials = oracleMaterials();
    REQUIRE(geometry.size() * materials.size() == 64);

    std::vector<PunctualSample> samples;
    std::vector<std::string> names;
    std::vector<Agreement> agreements;
    std::vector<glm::vec3> reference;
    std::vector<glm::vec3> bound;
    samples.reserve(64);
    for (const Geometry& place : geometry) {
        for (const Material& material : materials) {
            const PunctualSample sample = sampleOf(place.light, place.surface, material);
            names.push_back(place.name + " / " + material.name);
            agreements.push_back(place.agreement);
            reference.push_back(mirrorOf(sample));
            bound.push_back(
                place.agreement == Agreement::ConeEdgeBound
                    ? glm::abs(mirrorOf(sampleOf(place.light, place.innerEdge, material))) *
                          kConeEdgeLeakRatio
                    : glm::vec3(0.0f));
            samples.push_back(sample);
        }
    }

    // The table is only as good as its coverage, so state what it must contain. Five configurations
    // take an ordered early-out and one sits on the outer cone edge; neither depends on the
    // material, so each contributes all four of its samples.
    size_t exactZeros = 0;
    size_t coneEdges = 0;
    for (size_t index = 0; index < samples.size(); ++index) {
        exactZeros += agreements[index] == Agreement::ExactZero ? 1 : 0;
        coneEdges += agreements[index] == Agreement::ConeEdgeBound ? 1 : 0;
        if (agreements[index] == Agreement::ExactZero) {
            INFO("sample " << index << ": " << names[index]);
            REQUIRE(reference[index] == glm::vec3(0.0f));
        } else if (agreements[index] == Agreement::Tolerance) {
            INFO("sample " << index << ": " << names[index]);
            REQUIRE(reference[index] != glm::vec3(0.0f));
        }
    }
    REQUIRE(exactZeros == 20);
    REQUIRE(coneEdges == 4);

    auto device = rojoRHI::createDevice();
    INFO(errorOf(device));
    REQUIRE(device);
    auto library = (*device)->loadShaderLibrary("Shaders/PunctualLightOracle");
    INFO(errorOf(library));
    REQUIRE(library);
    auto pipeline = (*device)->createComputePipeline({.library = library->get(),
                                                      .computeEntry = "computeMain",
                                                      .threadsPerThreadgroup = {1, 1, 1},
                                                      .label = "lmx.test.punctualLight.pipeline"});
    INFO(errorOf(pipeline));
    REQUIRE(pipeline);

    auto sampleBuffer = (*device)->createBuffer({.size = samples.size() * sizeof(PunctualSample),
                                                 .label = "lmx.test.punctualLight.samples"},
                                                samples.data());
    INFO(errorOf(sampleBuffer));
    REQUIRE(sampleBuffer);
    auto output = (*device)->createBuffer({.size = samples.size() * sizeof(glm::vec4),
                                           .storageWrite = true,
                                           .cpuReadback = true,
                                           .label = "lmx.test.punctualLight.output"},
                                          nullptr);
    INFO(errorOf(output));
    REQUIRE(output);

    constexpr uint32_t kSampleSlot = 1;
    rojoRHI::CommandList& commands = (*device)->beginFrame();
    commands.beginComputePass("lmx.test.punctualLight.evaluate");
    commands.bindComputePipeline(**pipeline);
    commands.bindStorageBuffer(0, **output, rojoRHI::StorageAccess::Write);
    commands.bindBuffer(kSampleSlot, **sampleBuffer);
    commands.dispatch(static_cast<uint32_t>(samples.size()), 1, 1);
    commands.endComputePass();
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<glm::vec4> actual(samples.size());
    (*output)->readback(actual.data(), actual.size() * sizeof(glm::vec4));

    for (size_t index = 0; index < samples.size(); ++index) {
        INFO("sample " << index << ": " << names[index]);
        const glm::vec3 expected = reference[index];
        const glm::vec3 measured(actual[index]);
        for (int channel = 0; channel < 3; ++channel) {
            INFO("channel " << channel << ": cpu " << expected[channel] << " gpu "
                            << measured[channel]);
            switch (agreements[index]) {
            case Agreement::ExactZero:
                REQUIRE(measured[channel] == 0.0f);
                break;
            case Agreement::ConeEdgeBound:
                REQUIRE(bound[index][channel] > 0.0f);
                REQUIRE(std::abs(measured[channel]) <= bound[index][channel]);
                REQUIRE(std::abs(expected[channel]) <= bound[index][channel]);
                break;
            case Agreement::Tolerance: {
                const float scale = std::max(
                    {std::abs(expected[channel]), std::abs(measured[channel]), kAbsoluteFloor});
                REQUIRE(std::abs(measured[channel] - expected[channel]) <=
                        kRelativeTolerance * scale);
                break;
            }
            }
        }
    }
}
