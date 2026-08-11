//----------------------------------------------------------------------------------------------------------------------
/// @file RhiAdapter.cpp
/// @brief Implements the maintained-RHI adapter.
///
/// Per-frame work placement against the spec section 8 timed region:
///
///   BEFORE the timed region (setup(), called once): device creation; every pipeline compile;
///   every GPU resource creation and its one-time upload (R1-R10, the material and IBL textures,
///   the shared quad); and the one-time render-graph compile (compileSchedule()) that derives this
///   frame's fixed pass order and barrier list -- the graph planner runs exactly once here, never
///   per frame (spec: "the graph planner must NOT run per-frame or inside any timed region; the
///   schedule is data from the manifest"). refreshEmissiveStaging() also pre-populates R10's three
///   ring slots here so compileSchedule() has a live object to import, but this is throwaway
///   placeholder content -- runFrame() overwrites the active slot before every frame's P02 runs.
///
///   INSIDE the timed region (runFrame(), between beginFrame() and endFrame()): the fixed P01-P13
///   replay -- barrier emission, pipeline binds, every setUniforms/bindTexture/bindBuffer/
///   bindStorageBuffer/bindStorageTexture call, every draw/dispatch/copy, and the submit
///   (endFrame()). refreshEmissiveStaging() -- recreating R10's active ring slot with this frame's
///   deterministic staging content -- also runs inside the timed region: it stands in for a
///   per-frame ring *write*, which the spec explicitly keeps timed ("binding and root-data work
///   cannot be moved outside the timed region"), and the public RHI offers no way to write into an
///   existing buffer after creation (Buffer exposes only size() and a post-completion readback();
///   the only host-write path at all is Device::createBuffer's initialData). Recreating the R10
///   buffer that holds it is the closest legal analogue to rewriting a ring slot through this RHI,
///   and it is what production does *not* have an equivalent of at all -- no shipped pass reuploads
///   a texture every frame -- so this is a place M5.1's synthetic workload asks for something
///   production never needed, not a place this adapter chose a shortcut production avoids.
///
///   FINDING -- P04's per-draw b1 uniforms do not fit the shipped uniform ring, and are delivered
///   through freshly-created buffers instead. `RHI/Backends/Metal4/Source/Metal4Device.h`'s
///   `kUniformRingBytes` is a fixed 256 KiB per-frame budget, and every `setUniforms` call rounds
///   up to a 256-byte-aligned slot regardless of its own size (`kUniformOffsetAlignment`). P04
///   alone -- 1,024 draws x align(sizeof(ObjectUniforms) = 304, 256) = 512 bytes -- needs 512 KiB,
///   twice the *entire* ring, before P03, the pass uniforms, or a single P05-P12 kernel parameter
///   ever gets a byte; P03 alone -- 1,024 x align(64, 256) = 256 bytes -- exactly exhausts the ring
///   by itself, leaving zero bytes for anything else in the frame. Measured directly: a first
///   implementation that called `setUniforms` for every one of P03's and P04's 2,048 draws hit
///   "setUniforms: per-frame uniform ring exhausted" on P03's own last draw, before P04 ever
///   opened. No CPU-side economy closes a 2x-and-100%-simultaneously overflow, and this adapter may
///   not change `RHI/Backends` or the frozen manifest's 1,024-draw count (spec section 2's
///   production-tree and workload freezes) to fit it. The only other production-available way to
///   vary buffer slot 1's contents per draw under the unmodified ScenePass.slang/ShadowPass.slang
///   contract is `rhi::CommandList::bindBuffer`, which needs a real, already-created `rhi::Buffer`
///   per draw rather than a ring slice -- `rhi::Buffer`'s public surface is otherwise immutable
///   after creation (size() and a post-completion readback() only; the sole host-write path
///   anywhere in the public RHI is `Device::createBuffer`'s `initialData`), so a fresh buffer per
///   draw per frame is not a convenience, it is the only legal way to change what slot 1 holds at
///   all.
///
///   Placement of that per-draw delivery against the timed region is exact, not approximate, and
///   the two passes land on opposite sides of it precisely because of what varies per frame and
///   what does not: the shadow map's light is fixed for this manifest's whole 32-frame run, so
///   every one of P03's 1,024 draws has a frame-invariant mvp, and
///   createShadowObjectUniformBuffers() builds all 1,024 of those buffers exactly once, in setup(),
///   before the timed region -- ordinary one-time resource creation, which spec section 8 excludes
///   explicitly. P04's mvp is not frame-invariant (the camera orbits every frame), so its per-draw
///   block -- the createBuffer call that writes this frame's mvp alongside every other field, and
///   the bindBuffer that hands the result to the draw -- cannot be hoisted out of runFrame() the
///   way P03's was: encodeScene() creates, writes, and binds all 1,024 of P04's buffers inside the
///   beginFrame()/endFrame() span, every frame, alongside every other per-frame binding call this
///   file's timed-region comment above already places there. This is a materially larger and slower
///   substitute for the ring production actually uses (1,024 buffer allocations every frame for P04
///   alone versus zero once P03's are amortized, all of it CPU-side since
///   Metal4Device::createBuffer's shared-storage buffers need no GPU blit), and it is exactly the
///   kind of thing Stage 4's binding-traffic and CPU-encoding measurements will need to account for
///   rather than average away: this adapter's per-frame allocation-call count for P04 is not
///   comparable to a hypothetical unlimited-ring implementation, only to what the shipped RHI can
///   actually do, and Stage 4's timed region must wrap exactly the span encodeScene() already
///   occupies here -- P04 buffer creation included, P03 buffer creation excluded as one-time setup.
///
///   RESOLVED -- Workload/AssetGen.cpp's sharedQuad() wound its two triangles clockwise as seen
///   from above, contradicting its own comment ("+Y normal, CCW winding when viewed from above,
///   matching makePlane's convention") and Source/Render/Mesh.cpp's actual makePlane/addFace
///   convention (addFace's own comment: "cross(u, v) == normal gives every generated face an
///   outward CCW winding"). Found two ways: by hand (cross(v1 - v0, v2 - v0) on the original index
///   order {0, 1, 2, 0, 2, 3} came out (0, -1, 0), the opposite sign of addFace's construction for
///   the equivalent triangle) and empirically (P04's production ScenePass pipeline, built with
///   production's own `CullMode::Back`, rendered every one of P04's 1,024 draws invisible for every
///   frame of a 32-frame run -- confirmed by a diagnostic readback of R2 straight after the scene
///   pass, which was the frame's clear colour verbatim, pixel for pixel, everywhere). The camera
///   policy this manifest freezes (Workload/DrawPolicy.h's cameraForFrame) only ever looks down at
///   the draw grid from above, so this was not a marginal, sometimes-culled case -- it removed
///   literally every visible triangle, every frame. Fixed at the source: sharedQuad()'s index order
///   is now {0, 2, 1, 0, 3, 2} (cross(v2 - v0, v1 - v0) and cross(v3 - v0, v2 - v0) both work out
///   to (0, 1, 0), matching addFace's convention exactly), a manifest correction rather than an
///   adapter-side cull-mode workaround: no scored data exists yet, so spec section 2's measurement
///   freeze gate does not apply, and this adapter's shadow and scene pipelines are built with
///   production's own `CullMode::Back` -- the same value Source/Render/Renderer.cpp bakes into both
///   pipeline descs -- with no compensating change anywhere in this file.
///
///   OUTSIDE the timed region, after it (runFrame(), after endFrame()): Device::waitIdle() and the
///   R9 buffer readback. Both are explicitly excluded by spec section 8 ("excludes pacing waits,
///   ... and readback waits").
///
/// This adapter waits idle after every single frame rather than only pacing three deep, which is a
/// deliberate choice for a *correctness* run (this stage), not a performance one: Stage 4 owns the
/// timed protocol (section 8's paired AB/BA repetitions), and nothing here is measured. Waiting
/// idle keeps every frame's command buffer fully retired before the next begins recording, which is
/// *why* this adapter's one-time schedule compile can safely reuse the same
/// single-frame-in-isolation import calls NoApiBench --check-manifest already verifies against the
/// manifest's frozen dump (Workload/RepresentativeGraph.h's expectedGraphDump()): no persistent
/// resource here is ever touched by two command buffers that could execute out of order, so the
/// cross-frame "previous use" imports production's real (genuinely pipelined) Renderer.cpp needs
/// for its own persistent targets are not needed for correctness here. A future
/// performance-protocol adapter that paces three deep instead of waiting idle every frame would
/// need to add them.
//----------------------------------------------------------------------------------------------------------------------

#include "Bench/RhiAdapter.h"

#include "Workload/AssetGen.h"
#include "Workload/DrawPolicy.h"
#include "Workload/ProdValues.h"

#include "Core/Assert.h"
#include "RHI/Validate.h"
#include "Render/GraphDump.h"
#include "Render/Renderer.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <iostream>
#include <map>
#include <set>

namespace lmx::noapi::bench {

namespace {

using namespace lmx::render;
namespace workload = lmx::noapi::workload;

//======================================================================================================================
rhi::Format mapFormat(workload::Format format) {
    switch (format) {
    case workload::Format::RGBA8Unorm:
        return rhi::Format::RGBA8Unorm;
    case workload::Format::RGBA8Unorm_sRGB:
        return rhi::Format::RGBA8Unorm_sRGB;
    case workload::Format::RGBA16Float:
        return rhi::Format::RGBA16Float;
    case workload::Format::RG16Float:
        return rhi::Format::RG16Float;
    case workload::Format::D32Float:
        return rhi::Format::D32Float;
    }
    LMX_ASSERT(false, "RhiAdapter: unhandled workload::Format");
    return rhi::Format::Unknown;
}

//======================================================================================================================
rhi::TextureDesc manifestTextureDesc(const workload::TextureResource& resource) {
    return {.width = resource.width,
            .height = resource.height,
            .format = mapFormat(resource.format),
            .kind = rhi::TextureKind::Tex2D,
            .mipLevels = resource.mipLevels,
            // The RHI unifies colour and depth render-target use under one flag; the manifest
            // separates them only for documentation (spec section 6's resource table).
            .renderTarget = resource.renderTarget || resource.depthTarget,
            .sampled = resource.sampled,
            .storageRead = resource.storageRead,
            .storageWrite = resource.storageWrite,
            .cpuReadback = resource.cpuReadback,
            .label = resource.name};
}

//======================================================================================================================
rhi::BufferDesc manifestBufferDesc(const workload::BufferResource& resource) {
    return {.size = resource.size,
            .storageRead = resource.storageRead,
            .storageWrite = resource.storageWrite,
            .cpuReadback = resource.cpuReadback,
            .label = resource.name};
}

//======================================================================================================================
rhi::TextureSubresourceRange mipRange(uint32_t level) {
    return {.baseMipLevel = level, .mipLevelCount = 1};
}

//======================================================================================================================
// M5.1 Stage 4 allocation instrumentation (spec section 9): the public RHI exposes only a texture's
// requested dimensions/format/mip/layer counts, never Metal's padded allocated size the way the
// prototype's AllocationRecord does -- see Bench/Metrics.h's AllocationSnapshot header comment for
// why this is a documented, unavoidable asymmetry rather than a like-for-like measurement. This
// sums the full mip chain's tightly packed byte footprint from those requested dimensions, times
// the array-layer count (six for a cubemap), for every format the manifest actually uses -- none of
// which are block-compressed, so bytesPerPixel's tightly-packed formula is exact here even though
// it would undercount a BC1 texture.
uint64_t textureRequestedBytes(const rhi::Texture& texture) {
    const uint32_t bytesPerTexel = rhi::bytesPerPixel(texture.format());
    uint64_t total = 0;
    for (uint32_t mip = 0; mip < texture.mipLevels(); ++mip) {
        const uint32_t width = rhi::mipExtent(texture.width(), mip);
        const uint32_t height = rhi::mipExtent(texture.height(), mip);
        total += uint64_t{width} * height * bytesPerTexel;
    }
    return total * texture.arrayLayers();
}

//======================================================================================================================
// Reversed infinite-far perspective for Metal's [0,1] clip depth, restated from
// Render/Camera.cpp::projectionMatrix so this adapter needs no Camera object (it has an eye/target
// pair from the manifest's frozen camera policy, not a yaw/pitch state to derive one from).
glm::mat4 reversedInfiniteFarProjection(float fovYRadians, float aspect, float nearZ) {
    const float tanHalfFovY = std::tan(fovYRadians * 0.5f);
    glm::mat4 projection{0.0f};
    projection[0][0] = 1.0f / (aspect * tanHalfFovY);
    projection[1][1] = 1.0f / tanHalfFovY;
    projection[2][3] = -1.0f;
    projection[3][2] = nearZ;
    return projection;
}

// Half the frozen 32x32 draw grid's diagonal, plus the quad's own half-extent -- a bounding sphere
// guaranteed to contain every instance drawPlacement() places (Workload/DrawPolicy.cpp).
constexpr float kGridHalfExtent = (workload::kDrawGridSize - 1) * 0.5f;
constexpr float kBoundingRadius = kGridHalfExtent * 1.5f + 1.0f;

const glm::vec3 kLightDirection = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));

// Mirrors Source/Render/Renderer.cpp's shadow depth bias exactly -- the shipped calibration for the
// production shadow pipeline this adapter binds unchanged.
constexpr rhi::DepthBias kShadowDepthBias{.constant = -4.0f, .slopeScale = -32.0f};

// Shaders/ScenePass.slang's texture/sampler/buffer slot map (Source/Render/Renderer.cpp's
// anonymous-namespace constants, restated here because they are not exported): this adapter binds
// the unmodified production ScenePass/ShadowPass pipelines and must match their compiled register
// numbers exactly.
constexpr uint32_t kVertexBufferSlot = 0;
constexpr uint32_t kObjectUniformsSlot = 1;
constexpr uint32_t kPassUniformsSlot = 2;
constexpr uint32_t kDiffuseTextureSlot = 0;
constexpr uint32_t kNormalTextureSlot = 1;
constexpr uint32_t kShadowTextureSlot = 3;
constexpr uint32_t kMetallicRoughnessTextureSlot = 4;
constexpr uint32_t kOcclusionTextureSlot = 5;
constexpr uint32_t kEmissiveTextureSlot = 6;
constexpr uint32_t kIrradianceTextureSlot = 7;
constexpr uint32_t kPrefilteredEnvTextureSlot = 8;
constexpr uint32_t kDfgLutTextureSlot = 9;
constexpr uint32_t kLinearSamplerSlot = 0;
constexpr uint32_t kShadowSamplerSlot = 1;
constexpr uint32_t kIblSamplerSlot = 2;
constexpr uint32_t kFlagHasNormalMap = 1u;

// Mirrors Shaders/ScenePass.slang's ObjectUniforms (Source/Render/Renderer.cpp's own local mirror).
struct ObjectUniforms {
    glm::mat4 mvp;
    glm::mat4 model;
    glm::mat4 normalMatrix;
    glm::mat4 uvTransform;
    glm::vec4 albedo;
    float roughness;
    uint32_t flags;
    float metallic;
    float occlusionStrength;
    glm::vec3 emissive;
    float emissivePadding;
};
static_assert(sizeof(ObjectUniforms) == 304);

struct ShadowObjectUniforms {
    glm::mat4 mvp;
};
static_assert(sizeof(ShadowObjectUniforms) == 64);

struct DirLightUniform {
    glm::vec3 strength;
    float strengthPadding;
    glm::vec3 direction;
    float directionPadding;
};
static_assert(sizeof(DirLightUniform) == 32);

// Mirrors Shaders/ScenePass.slang's PassUniforms.
struct PassUniforms {
    glm::mat4 viewProj;
    glm::mat4 shadowTransform;
    glm::vec3 eyePos;
    float eyePadding;
    float time;
    float preExposure;
    float alignmentPadding[2];
    DirLightUniform lights[3];
    int32_t shadowFilter;
    int32_t tailPadding[3];
};
static_assert(sizeof(PassUniforms) == 272);

constexpr int32_t kShadowFilterPcf = 0;

// This adapter's own P05-P12 kernel slot maps (Experiments/NoApi/Shaders/*.slang).
constexpr uint32_t kHistTextureSlot = 0;
constexpr uint32_t kHistBufferSlot = 0;
constexpr uint32_t kHistExposureSlot = 1;
constexpr uint32_t kHistParamsSlot = 2;
constexpr uint32_t kBloomThresholdSrcSlot = 0;
constexpr uint32_t kBloomThresholdDstSlot = 1;
constexpr uint32_t kBloomThresholdParamsSlot = 0;
constexpr uint32_t kBloomDownSrcSlot = 0;
constexpr uint32_t kBloomDownDstSlot = 1;
constexpr uint32_t kBloomDownParamsSlot = 0;
constexpr uint32_t kBloomUpBaseSlot = 0;
constexpr uint32_t kBloomUpSmallSlot = 1;
constexpr uint32_t kBloomUpDstSlot = 2;
constexpr uint32_t kBloomUpParamsSlot = 0;
constexpr uint32_t kCompositeSceneSlot = 0;
constexpr uint32_t kCompositeBloomSlot = 1;
constexpr uint32_t kCompositeExposureSlot = 0;
constexpr uint32_t kCompositeParamsSlot = 1;

struct HistParams {
    float logLuminanceMin;
    float logLuminanceMax;
};
struct ResolveParams {
    float lowPercentile;
    float highPercentile;
    float targetGrey;
    float evMin;
    float evMax;
    float compensationEv;
    float logLuminanceMin;
    float logLuminanceMax;
};
struct ThresholdParams {
    float threshold;
};
struct DownsampleParams {
    uint32_t srcWidth, srcHeight, dstWidth, dstHeight;
};
struct UpsampleParams {
    uint32_t smallWidth, smallHeight, dstWidth, dstHeight;
};
struct CompositeParams {
    float bloomIntensity;
};

constexpr float kExposureLogLuminanceMin = -12.0f;
constexpr float kExposureLogLuminanceMax = 4.0f;

//======================================================================================================================
rhi::Result<std::unique_ptr<rhi::Texture>>
createGeneratedTexture(rhi::Device& device, const workload::GeneratedTexture& generated,
                       rhi::Format format, std::string_view label) {
    std::vector<rhi::TextureMip> mips;
    mips.reserve(generated.mips.size());
    for (const workload::MipLevel& level : generated.mips) {
        mips.push_back({.data = level.rgba8.data(), .bytesPerRow = uint64_t{level.width} * 4});
    }
    return device.createTexture({.width = generated.mips.front().width,
                                 .height = generated.mips.front().height,
                                 .format = format,
                                 .kind = rhi::TextureKind::Tex2D,
                                 .mipLevels = static_cast<uint32_t>(generated.mips.size()),
                                 .sampled = true,
                                 .label = label},
                                mips);
}

//======================================================================================================================
rhi::Result<std::unique_ptr<rhi::Texture>> createCubeTexture(rhi::Device& device,
                                                             const workload::SyntheticCubemap& cube,
                                                             rhi::Format format,
                                                             std::string_view label) {
    const uint32_t mipCount = static_cast<uint32_t>(cube.mips.size());
    std::vector<rhi::TextureMip> mips;
    mips.reserve(size_t{6} * mipCount);
    // Device::createTexture wants mip-major-per-face ordering: face0[mip0..N], face1[mip0..N], ...
    for (uint32_t face = 0; face < 6; ++face) {
        for (uint32_t level = 0; level < mipCount; ++level) {
            const uint32_t faceSize = std::max(cube.faceSize >> level, 1u);
            mips.push_back({.data = cube.mips[level][face].texelsRgba16.data(),
                            .bytesPerRow = uint64_t{faceSize} * 8});
        }
    }
    return device.createTexture({.width = cube.faceSize,
                                 .height = cube.faceSize,
                                 .format = format,
                                 .kind = rhi::TextureKind::Cube,
                                 .mipLevels = mipCount,
                                 .sampled = true,
                                 .label = label},
                                mips);
}

} // namespace

//======================================================================================================================
bool RhiAdapter::setup() {
    m_diagScene = std::getenv("LMX_NOAPI_DIAG_SCENE") != nullptr;

    auto device = rhi::createDevice();
    if (!device) {
        std::cerr << "RhiAdapter::setup: createDevice failed: " << device.error().message << "\n";
        return false;
    }
    m_device = std::move(*device);

    // Placeholder content for all three R10 slots so compileSchedule()'s one-time graph compile has
    // a live buffer to import; runFrame() overwrites the active slot with real content every frame.
    for (uint32_t slot = 0; slot < workload::kEmissiveRingSlotCount; ++slot) {
        refreshEmissiveStaging(slot);
    }

    if (!createResources()) {
        return false;
    }
    if (!createPipelines()) {
        return false;
    }
    if (!createShadowObjectUniformBuffers()) {
        return false;
    }
    if (!compileSchedule()) {
        return false;
    }
    if (m_diagScene) {
        // Unscored: R2 is 1024x1024 RGBA16Float, twice R9's frozen 4 MiB, so the diagnostic gets
        // its own buffer rather than reinterpreting the manifest's.
        auto buffer = m_device->createBuffer(
            {.size = uint64_t{workload::kSceneWidth} * workload::kSceneHeight * 8,
             .cpuReadback = true,
             .label = "lmx.noapi.bench.diag.sceneColor"},
            nullptr);
        if (!buffer) {
            std::cerr << "RhiAdapter: failed to create the diagnostic buffer: "
                      << buffer.error().message << "\n";
            return false;
        }
        m_diagSceneColor = std::move(*buffer);
    }

    // M5.1 Stage 4 (spec section 9's allocation dimension): deterministic, one-time setup creation
    // counts derivable from the manifest and this adapter's own fixed structure.
    // createShadowObjectUniformBuffers() and every runFrame() buffer creation already incremented
    // m_creationCounters.bufferCreateCalls at their own real call sites (createShadowObjectUniform
    // Buffers above, encodeScene's per-draw P04 block, refreshEmissiveStaging's R10 recreation), so
    // this adds only what is counted nowhere else: the manifest's own textures/buffers, the
    // material/IBL textures, the shared quad, and the samplers -- all created exactly once, above,
    // unconditionally.
    const workload::RepresentativeGraph& manifestForCounting = workload::representativeGraph();
    m_creationCounters.textureCreateCalls += manifestForCounting.textures.size();
    m_creationCounters.textureCreateCalls += uint64_t{workload::kMaterialCount} * 4 +
                                             (workload::kMaterialCount - 1); // +emissive, mat!=0
    m_creationCounters.textureCreateCalls += 3; // irradiance, prefilteredEnv, dfgLut
    m_creationCounters.bufferCreateCalls +=
        manifestForCounting.buffers.size() - 1;  // R10 realised as m_r10Ring, not one buffer
    m_creationCounters.bufferCreateCalls += 2;   // shared quad: vertex + index
    m_creationCounters.samplerCreateCalls += 3;  // linear, shadow, ibl
    m_creationCounters.pipelineCreateCalls += 8; // 3 graphics (shadow/scene/composite) + 5 compute
    return true;
}

//======================================================================================================================
bool RhiAdapter::createShadowObjectUniformBuffers() {
    const ShadowMatrices shadow =
        fitShadowOrtho(glm::vec4(0.0f, 0.0f, 0.0f, kBoundingRadius), kLightDirection);
    for (uint32_t draw = 0; draw < workload::kDrawCount; ++draw) {
        const workload::DrawPlacement placement = workload::drawPlacement(draw);
        const glm::mat4 model =
            glm::translate(glm::mat4(1.0f), glm::vec3(placement.x, 0.0f, placement.z));
        const ShadowObjectUniforms uniforms{.mvp = shadow.viewProj * model};
        auto buffer = m_device->createBuffer(
            {.size = sizeof(uniforms), .label = "lmx.noapi.bench.shadow.objectUniforms"},
            &uniforms);
        if (!buffer) {
            std::cerr << "RhiAdapter: failed to create shadow object-uniform buffer " << draw
                      << ": " << buffer.error().message << "\n";
            return false;
        }
        m_shadowObjectUniformBuffers[draw] = std::move(*buffer);
        m_creationCounters.bufferCreateCalls += 1;
    }
    return true;
}

//======================================================================================================================
bool RhiAdapter::createResources() {
    const workload::RepresentativeGraph& manifest = workload::representativeGraph();

    for (const workload::TextureResource& resource : manifest.textures) {
        auto texture = m_device->createTexture(manifestTextureDesc(resource));
        if (!texture) {
            std::cerr << "RhiAdapter: failed to create texture '" << resource.name
                      << "': " << texture.error().message << "\n";
            return false;
        }
        if (resource.id == "R1") {
            m_r1Shadow = std::move(*texture);
        } else if (resource.id == "R2") {
            m_r2SceneColor = std::move(*texture);
        } else if (resource.id == "R3") {
            m_r3SceneDepth = std::move(*texture);
        } else if (resource.id == "R6") {
            m_r6BloomA = std::move(*texture);
        } else if (resource.id == "R7") {
            m_r7BloomB = std::move(*texture);
        } else if (resource.id == "R8") {
            m_r8Out = std::move(*texture);
        } else if (resource.id == workload::kMaterialZeroEmissiveId) {
            // Initial content matches AssetGen's own generator, so frame 0's shading sees a real
            // emissive texture even before its first mip-0 overwrite completes.
            const workload::GeneratedTexture generated =
                workload::generateMaterialTexture(0, workload::MaterialTextureSlot::Emissive);
            std::vector<rhi::TextureMip> mips;
            mips.reserve(generated.mips.size());
            for (const workload::MipLevel& level : generated.mips) {
                mips.push_back(
                    {.data = level.rgba8.data(), .bytesPerRow = uint64_t{level.width} * 4});
            }
            auto uploaded = m_device->createTexture(manifestTextureDesc(resource), mips);
            if (!uploaded) {
                std::cerr << "RhiAdapter: failed to create material0.emissive: "
                          << uploaded.error().message << "\n";
                return false;
            }
            m_material0Emissive = std::move(*uploaded);
        } else {
            LMX_ASSERT(false, "RhiAdapter: unhandled manifest texture id");
        }
    }

    for (const workload::BufferResource& resource : manifest.buffers) {
        if (resource.id == "R10") {
            continue; // Realised as the rotating m_r10Ring, not one 48 KiB buffer.
        }
        const float initialExposure = 1.0f;
        const void* initialData = resource.id == "R5" ? &initialExposure : nullptr;
        auto buffer = m_device->createBuffer(manifestBufferDesc(resource), initialData);
        if (!buffer) {
            std::cerr << "RhiAdapter: failed to create buffer '" << resource.name
                      << "': " << buffer.error().message << "\n";
            return false;
        }
        if (resource.id == "R4") {
            m_r4Histogram = std::move(*buffer);
        } else if (resource.id == "R5") {
            m_r5Exposure = std::move(*buffer);
        } else if (resource.id == "R9") {
            m_r9Readback = std::move(*buffer);
        } else {
            LMX_ASSERT(false, "RhiAdapter: unhandled manifest buffer id");
        }
    }

    // Materials, IBL, and the shared quad are not part of the graph-tracked resource table (spec
    // section 6/RepresentativeGraph.h's kMaterialZeroEmissiveId doc comment): they are read-only
    // for the whole run, generated once here, and bound directly at execute time exactly as
    // production binds its own per-material and per-pass shared texture sets.
    for (uint32_t material = 0; material < workload::kMaterialCount; ++material) {
        MaterialTextures& textures = m_materials[material];
        const auto makeSlot =
            [&](workload::MaterialTextureSlot slot, workload::Format format,
                const char* suffix) -> rhi::Result<std::unique_ptr<rhi::Texture>> {
            return createGeneratedTexture(
                *m_device, workload::generateMaterialTexture(material, slot), mapFormat(format),
                std::format("lmx.noapi.bench.material{}.{}", material, suffix));
        };
        auto baseColor = makeSlot(workload::MaterialTextureSlot::BaseColor,
                                  workload::prod::kBaseColorFormat, "baseColor");
        auto normal = makeSlot(workload::MaterialTextureSlot::Normal, workload::prod::kNormalFormat,
                               "normal");
        auto metallicRoughness =
            makeSlot(workload::MaterialTextureSlot::MetallicRoughness,
                     workload::prod::kMetallicRoughnessFormat, "metallicRoughness");
        auto occlusion = makeSlot(workload::MaterialTextureSlot::Occlusion,
                                  workload::prod::kOcclusionFormat, "occlusion");
        if (!baseColor || !normal || !metallicRoughness || !occlusion) {
            std::cerr << "RhiAdapter: failed to create material " << material << " textures\n";
            return false;
        }
        textures.baseColor = std::move(*baseColor);
        textures.normal = std::move(*normal);
        textures.metallicRoughness = std::move(*metallicRoughness);
        textures.occlusion = std::move(*occlusion);
        if (material != 0) {
            // Material 0's emissive slot is the tracked kMaterialZeroEmissiveId texture instead.
            auto emissive = makeSlot(workload::MaterialTextureSlot::Emissive,
                                     workload::prod::kEmissiveFormat, "emissive");
            if (!emissive) {
                std::cerr << "RhiAdapter: failed to create material " << material << " emissive\n";
                return false;
            }
            textures.emissive = std::move(*emissive);
        }
    }

    auto irradiance = createCubeTexture(*m_device, workload::generateSyntheticIrradiance(),
                                        mapFormat(workload::prod::kIrradianceFormat),
                                        "lmx.noapi.bench.irradiance");
    auto prefilteredEnv = createCubeTexture(*m_device, workload::generateSyntheticPrefilteredEnv(),
                                            mapFormat(workload::prod::kPrefilteredFormat),
                                            "lmx.noapi.bench.prefilteredEnv");
    if (!irradiance || !prefilteredEnv) {
        std::cerr << "RhiAdapter: failed to create IBL cubemaps\n";
        return false;
    }
    m_irradiance = std::move(*irradiance);
    m_prefilteredEnv = std::move(*prefilteredEnv);

    {
        const workload::DfgLut lut = workload::generateSyntheticDfgLut();
        const rhi::TextureMip mip{.data = lut.texelsRg16.data(),
                                  .bytesPerRow = uint64_t{lut.size} * 4};
        auto dfgLut = m_device->createTexture({.width = lut.size,
                                               .height = lut.size,
                                               .format = mapFormat(workload::prod::kDfgLutFormat),
                                               .sampled = true,
                                               .label = "lmx.noapi.bench.dfgLut"},
                                              std::span{&mip, 1});
        if (!dfgLut) {
            std::cerr << "RhiAdapter: failed to create DFG LUT: " << dfgLut.error().message << "\n";
            return false;
        }
        m_dfgLut = std::move(*dfgLut);
    }

    {
        const workload::QuadGeometry quad = workload::sharedQuad();
        auto vertexBuffer = m_device->createBuffer(
            {.size = sizeof(quad.vertices), .label = "lmx.noapi.bench.quadVertices"},
            quad.vertices.data());
        auto indexBuffer = m_device->createBuffer(
            {.size = sizeof(quad.indices), .label = "lmx.noapi.bench.quadIndices"},
            quad.indices.data());
        if (!vertexBuffer || !indexBuffer) {
            std::cerr << "RhiAdapter: failed to create the shared quad\n";
            return false;
        }
        m_quadVertexBuffer = std::move(*vertexBuffer);
        m_quadIndexBuffer = std::move(*indexBuffer);
    }

    auto linearSampler = m_device->createSampler({.filter = rhi::FilterMode::Linear,
                                                  .addressMode = rhi::AddressMode::Wrap,
                                                  .maxAnisotropy = 16,
                                                  .label = "lmx.noapi.bench.linearSampler"});
    auto shadowSampler = m_device->createSampler({.filter = rhi::FilterMode::Linear,
                                                  .addressMode = rhi::AddressMode::Clamp,
                                                  .maxAnisotropy = 16,
                                                  .compare = rhi::CompareFunc::GreaterEqual,
                                                  .label = "lmx.noapi.bench.shadowSampler"});
    auto iblSampler = m_device->createSampler({.filter = rhi::FilterMode::Linear,
                                               .addressMode = rhi::AddressMode::Clamp,
                                               .label = "lmx.noapi.bench.iblSampler"});
    if (!linearSampler || !shadowSampler || !iblSampler) {
        std::cerr << "RhiAdapter: failed to create samplers\n";
        return false;
    }
    m_linearSampler = std::move(*linearSampler);
    m_shadowSampler = std::move(*shadowSampler);
    m_iblSampler = std::move(*iblSampler);

    return true;
}

//======================================================================================================================
bool RhiAdapter::createPipelines() {
    // M5.1 Stage 4 (spec section 8's pipeline dimension, descriptive only): loadShaderLibrary is
    // where the real compile cost lives -- creating a rhi::GraphicsPipeline/ComputePipeline from an
    // already-loaded library is comparatively cheap state assembly -- so this is timed here, once
    // per shader, in this adapter's one-time setup(). "Cold" per the spec's own definition (first
    // creation in a fresh process) holds by construction: createPipelines() runs exactly once per
    // process. metallib-vs-runtime-MSL is read off the same file-existence check
    // NoApiAdapter.cpp's readShader() uses, rather than a query this RHI does not expose.
    const auto load = [&](std::string_view path, std::unique_ptr<rhi::ShaderLibrary>& out) {
        const auto start = std::chrono::steady_clock::now();
        auto library = m_device->loadShaderLibrary(path);
        const auto end = std::chrono::steady_clock::now();
        if (!library) {
            std::cerr << "RhiAdapter: failed to load '" << path << "': " << library.error().message
                      << "\n";
            return false;
        }
        out = std::move(*library);
        std::error_code errorCode;
        const bool loadedMetallib =
            std::filesystem::exists(std::string(path) + ".metallib", errorCode);
        m_pipelineCompileTimes.push_back(
            {.label = std::string(path),
             .coldNs = static_cast<uint64_t>(
                 std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()),
             .loadedMetallib = loadedMetallib});
        return true;
    };
    if (!load("Shaders/ShadowPass", m_shadowLibrary) ||
        !load("Shaders/ScenePass", m_sceneLibrary) ||
        !load("Shaders/HistAccumulate", m_histAccumulateLibrary) ||
        !load("Shaders/HistResolve", m_histResolveLibrary) ||
        !load("Shaders/BloomThreshold", m_bloomThresholdLibrary) ||
        !load("Shaders/BloomDown", m_bloomDownLibrary) ||
        !load("Shaders/BloomUp", m_bloomUpLibrary) ||
        !load("Shaders/Composite", m_compositeLibrary)) {
        return false;
    }

    auto shadowPipeline =
        m_device->createGraphicsPipeline({.library = m_shadowLibrary.get(),
                                          .vertexEntry = "vertexMain",
                                          .fragmentEntry = "fragmentMain",
                                          .colorFormat = rhi::Format::Unknown,
                                          .depthFormat = rhi::Format::D32Float,
                                          .depthTestEnable = true,
                                          .depthWriteEnable = true,
                                          .cullMode = rhi::CullMode::Back,
                                          .depthCompare = rhi::DepthCompare::Greater,
                                          .depthBias = kShadowDepthBias,
                                          .label = "lmx.noapi.bench.shadowPipeline"});
    auto scenePipeline =
        m_device->createGraphicsPipeline({.library = m_sceneLibrary.get(),
                                          .vertexEntry = "vertexMain",
                                          .fragmentEntry = "fragmentMain",
                                          .colorFormat = rhi::Format::RGBA16Float,
                                          .depthFormat = rhi::Format::D32Float,
                                          .depthTestEnable = true,
                                          .depthWriteEnable = true,
                                          .cullMode = rhi::CullMode::Back,
                                          .depthCompare = rhi::DepthCompare::Greater,
                                          .label = "lmx.noapi.bench.scenePipeline"});
    auto compositePipeline =
        m_device->createGraphicsPipeline({.library = m_compositeLibrary.get(),
                                          .vertexEntry = "vertexMain",
                                          .fragmentEntry = "fragmentMain",
                                          .colorFormat = rhi::Format::RGBA8Unorm,
                                          .depthFormat = rhi::Format::Unknown,
                                          .cullMode = rhi::CullMode::None,
                                          .label = "lmx.noapi.bench.compositePipeline"});
    if (!shadowPipeline || !scenePipeline || !compositePipeline) {
        std::cerr << "RhiAdapter: failed to create a raster pipeline\n";
        return false;
    }
    m_shadowPipeline = std::move(*shadowPipeline);
    m_scenePipeline = std::move(*scenePipeline);
    m_compositePipeline = std::move(*compositePipeline);

    constexpr uint32_t kThreads2D = 8;
    auto histAccumulate =
        m_device->createComputePipeline({.library = m_histAccumulateLibrary.get(),
                                         .computeEntry = "computeHistAccumulate",
                                         .threadsPerThreadgroup = {kThreads2D, kThreads2D, 1},
                                         .label = "lmx.noapi.bench.histAccumulatePipeline"});
    auto histResolve =
        m_device->createComputePipeline({.library = m_histResolveLibrary.get(),
                                         .computeEntry = "computeHistResolve",
                                         .threadsPerThreadgroup = {1, 1, 1},
                                         .label = "lmx.noapi.bench.histResolvePipeline"});
    auto bloomThreshold =
        m_device->createComputePipeline({.library = m_bloomThresholdLibrary.get(),
                                         .computeEntry = "computeBloomThreshold",
                                         .threadsPerThreadgroup = {kThreads2D, kThreads2D, 1},
                                         .label = "lmx.noapi.bench.bloomThresholdPipeline"});
    auto bloomDown =
        m_device->createComputePipeline({.library = m_bloomDownLibrary.get(),
                                         .computeEntry = "computeBloomDown",
                                         .threadsPerThreadgroup = {kThreads2D, kThreads2D, 1},
                                         .label = "lmx.noapi.bench.bloomDownPipeline"});
    auto bloomUp =
        m_device->createComputePipeline({.library = m_bloomUpLibrary.get(),
                                         .computeEntry = "computeBloomUp",
                                         .threadsPerThreadgroup = {kThreads2D, kThreads2D, 1},
                                         .label = "lmx.noapi.bench.bloomUpPipeline"});
    if (!histAccumulate || !histResolve || !bloomThreshold || !bloomDown || !bloomUp) {
        std::cerr << "RhiAdapter: failed to create a compute pipeline\n";
        return false;
    }
    m_histAccumulatePipeline = std::move(*histAccumulate);
    m_histResolvePipeline = std::move(*histResolve);
    m_bloomThresholdPipeline = std::move(*bloomThreshold);
    m_bloomDownPipeline = std::move(*bloomDown);
    m_bloomUpPipeline = std::move(*bloomUp);

    return true;
}

//======================================================================================================================
bool RhiAdapter::compileSchedule() {
    const workload::RepresentativeGraph& manifest = workload::representativeGraph();

    const auto textureFor = [&](const std::string& id) -> rhi::Texture* {
        if (id == "R1")
            return m_r1Shadow.get();
        if (id == "R2")
            return m_r2SceneColor.get();
        if (id == "R3")
            return m_r3SceneDepth.get();
        if (id == "R6")
            return m_r6BloomA.get();
        if (id == "R7")
            return m_r7BloomB.get();
        if (id == "R8")
            return m_r8Out.get();
        if (id == workload::kMaterialZeroEmissiveId)
            return m_material0Emissive.get();
        return nullptr;
    };
    const auto bufferFor = [&](const std::string& id) -> rhi::Buffer* {
        if (id == "R4")
            return m_r4Histogram.get();
        if (id == "R5")
            return m_r5Exposure.get();
        if (id == "R9")
            return m_r9Readback.get();
        if (id == "R10")
            return m_r10Ring[0].get();
        return nullptr;
    };

    // Real-resource restatement of NoApiBenchMain.cpp's declareToRenderGraph, over this adapter's
    // own live GPU objects instead of FakeTexture/FakeBuffer stand-ins -- so the schedule and
    // barriers this adapter replays every frame are derived from exactly what it executes, and
    // compared against the same frozen expectation --check-manifest verifies.
    RenderGraph graph;
    std::vector<std::variant<rhi::Texture*, rhi::Buffer*>> resourceByIndex;
    std::map<std::string, GraphTexture> textureBase;
    std::map<std::string, uint32_t> textureVersion;
    std::map<std::string, GraphBuffer> bufferBase;
    std::map<std::string, uint32_t> bufferVersion;

    for (const workload::TextureResource& resource : manifest.textures) {
        rhi::Texture* texture = textureFor(resource.id);
        LMX_ASSERT(texture != nullptr, "RhiAdapter: unmapped manifest texture id");
        const GraphTexture handle =
            graph.importTexture(*texture, mapFormat(resource.format), resource.name);
        textureBase[resource.id] = handle;
        textureVersion[resource.id] = 0;
        resourceByIndex.push_back(texture);
    }
    for (const workload::BufferResource& resource : manifest.buffers) {
        rhi::Buffer* buffer = bufferFor(resource.id);
        LMX_ASSERT(buffer != nullptr, "RhiAdapter: unmapped manifest buffer id");
        const GraphBuffer handle =
            resource.persistent
                ? graph.importBuffer(*buffer, resource.name, rhi::BufferUse::StorageWrite)
                : graph.importBuffer(*buffer, resource.name);
        bufferBase[resource.id] = handle;
        bufferVersion[resource.id] = 0;
        resourceByIndex.push_back(buffer);
    }

    const auto currentTexture = [&](const std::string& id) {
        return GraphTexture{textureBase.at(id).index, textureVersion.at(id)};
    };
    const auto currentBuffer = [&](const std::string& id) {
        return GraphBuffer{bufferBase.at(id).index, bufferVersion.at(id)};
    };

    std::set<std::string> readElsewhere;
    for (const workload::PassDeclaration& pass : manifest.passes) {
        for (const workload::ResourceUse& use : pass.uses) {
            if (use.role == workload::UseRole::Read || use.role == workload::UseRole::ShaderRead ||
                use.role == workload::UseRole::CopySource) {
                readElsewhere.insert(use.resourceId);
            }
        }
    }

    const ExecuteFn kNoWork = [](const PassResources&) {};
    for (const workload::PassDeclaration& pass : manifest.passes) {
        std::vector<std::string> writtenTextures;
        std::vector<std::string> writtenBuffers;
        const auto range = [](const workload::SubresourceRange& r) {
            return rhi::TextureSubresourceRange{
                .baseMipLevel = r.baseMipLevel,
                .mipLevelCount = r.mipLevelCount == 0 ? rhi::kAllMipLevels : r.mipLevelCount};
        };

        if (pass.kind == workload::PassKind::Raster) {
            PassDesc desc;
            for (const workload::ResourceUse& use : pass.uses) {
                const bool texture = textureBase.contains(use.resourceId);
                switch (use.role) {
                case workload::UseRole::Read:
                    if (texture) {
                        desc.textureReads.push_back(
                            TextureUseDesc(currentTexture(use.resourceId), range(use.range)));
                    } else {
                        desc.bufferReads.push_back(currentBuffer(use.resourceId));
                    }
                    break;
                case workload::UseRole::ColorAttachment:
                    desc.color = ColorAttachment{.handle = currentTexture(use.resourceId)};
                    writtenTextures.push_back(use.resourceId);
                    break;
                case workload::UseRole::DepthAttachment:
                    desc.depth = DepthAttachment{.handle = currentTexture(use.resourceId),
                                                 .store = readElsewhere.contains(use.resourceId)
                                                              ? StoreOp::Store
                                                              : StoreOp::Discard};
                    writtenTextures.push_back(use.resourceId);
                    break;
                default:
                    LMX_ASSERT(false, "RhiAdapter: unsupported raster use role in manifest");
                }
            }
            graph.addPass(pass.label, std::move(desc), kNoWork);
        } else if (pass.kind == workload::PassKind::Compute) {
            ComputePassDesc desc;
            for (const workload::ResourceUse& use : pass.uses) {
                const bool texture = textureBase.contains(use.resourceId);
                switch (use.role) {
                case workload::UseRole::Read:
                    if (texture) {
                        desc.textureReads.push_back(
                            TextureUseDesc(currentTexture(use.resourceId), range(use.range)));
                    } else {
                        desc.bufferReads.push_back(currentBuffer(use.resourceId));
                    }
                    break;
                case workload::UseRole::ShaderRead:
                    if (texture) {
                        desc.shaderTextureReads.push_back(
                            TextureUseDesc(currentTexture(use.resourceId), range(use.range)));
                    } else {
                        desc.shaderBufferReads.push_back(currentBuffer(use.resourceId));
                    }
                    break;
                case workload::UseRole::Write:
                    if (texture) {
                        desc.textureWrites.push_back(
                            TextureUseDesc(currentTexture(use.resourceId), range(use.range)));
                        writtenTextures.push_back(use.resourceId);
                    } else {
                        desc.bufferWrites.push_back(currentBuffer(use.resourceId));
                        writtenBuffers.push_back(use.resourceId);
                    }
                    break;
                default:
                    LMX_ASSERT(false, "RhiAdapter: unsupported compute use role in manifest");
                }
            }
            graph.addComputePass(pass.label, std::move(desc), kNoWork);
        } else {
            CopyPassDesc desc;
            for (const workload::ResourceUse& use : pass.uses) {
                const bool texture = textureBase.contains(use.resourceId);
                switch (use.role) {
                case workload::UseRole::CopySource:
                    if (texture) {
                        desc.textureSources.push_back(
                            TextureUseDesc(currentTexture(use.resourceId), range(use.range)));
                    } else {
                        desc.bufferSources.push_back(currentBuffer(use.resourceId));
                    }
                    break;
                case workload::UseRole::CopyDestination:
                    if (texture) {
                        desc.textureDestinations.push_back(
                            TextureUseDesc(currentTexture(use.resourceId), range(use.range)));
                        writtenTextures.push_back(use.resourceId);
                    } else {
                        desc.bufferDestinations.push_back(currentBuffer(use.resourceId));
                        writtenBuffers.push_back(use.resourceId);
                    }
                    break;
                default:
                    LMX_ASSERT(false, "RhiAdapter: unsupported copy use role in manifest");
                }
            }
            graph.addCopyPass(pass.label, std::move(desc), kNoWork);
        }

        for (const std::string& id : writtenTextures)
            textureVersion[id] += 1;
        for (const std::string& id : writtenBuffers)
            bufferVersion[id] += 1;
    }

    for (const workload::SinkDeclaration& sink : manifest.sinks) {
        if (textureBase.contains(sink.resourceId)) {
            const GraphTexture handle = currentTexture(sink.resourceId);
            sink.kind == workload::SinkKind::Export ? graph.exportTexture(handle)
                                                    : graph.readbackTexture(handle);
        } else {
            const GraphBuffer handle = currentBuffer(sink.resourceId);
            sink.kind == workload::SinkKind::Export ? graph.exportBuffer(handle)
                                                    : graph.readbackBuffer(handle);
        }
    }

    const GraphResult<CompiledFrameRecord> compiled = graph.compileFrame(0);
    if (!compiled) {
        std::cerr << "RhiAdapter: manifest graph failed to compile: " << compiled.error().message
                  << "\n";
        return false;
    }

    std::vector<std::string> actualOrder;
    for (uint32_t passIndex : compiled->debug.schedule.passes) {
        actualOrder.push_back(manifest.passes.at(passIndex).id);
    }
    if (actualOrder != workload::expectedScheduleOrder()) {
        std::cerr << "RhiAdapter: compiled schedule does not match the manifest's frozen order\n";
        return false;
    }
    if (dumpCompiledFrame(*compiled) != workload::expectedGraphDump()) {
        std::cerr << "RhiAdapter: compiled graph dump does not match the manifest's frozen "
                     "expectation\n";
        return false;
    }

    for (const DebugTransition& transition : compiled->debug.transitions) {
        BarrierOp op;
        if (transition.kind == GraphResourceKind::Texture) {
            rhi::Texture* texture =
                std::get<rhi::Texture*>(resourceByIndex.at(transition.resource));
            op = TextureBarrierOp{texture, transition.range, transition.textureFrom,
                                  transition.textureTo};
        } else {
            rhi::Buffer* buffer = std::get<rhi::Buffer*>(resourceByIndex.at(transition.resource));
            op = BufferBarrierOp{buffer, transition.bufferFrom, transition.bufferTo};
        }
        m_barriersBeforePass.at(transition.beforePass).push_back(op);
    }

    return true;
}

//======================================================================================================================
void RhiAdapter::refreshEmissiveStaging(uint32_t frameIndex) {
    const uint32_t slot = frameIndex % workload::kEmissiveRingSlotCount;
    std::vector<uint8_t> bytes(workload::kEmissiveRingSlotSize);
    for (uint32_t y = 0; y < workload::kMaterialTextureSize; ++y) {
        for (uint32_t x = 0; x < workload::kMaterialTextureSize; ++x) {
            const workload::EmissiveStagingTexel texel =
                workload::emissiveStagingTexel(frameIndex, x, y);
            const size_t index = (size_t{y} * workload::kMaterialTextureSize + x) * 4;
            bytes[index + 0] = texel.r;
            bytes[index + 1] = texel.g;
            bytes[index + 2] = texel.b;
            bytes[index + 3] = texel.a;
        }
    }
    auto buffer = m_device->createBuffer(
        {.size = workload::kEmissiveRingSlotSize, .label = "lmx.noapi.bench.emissiveStaging"},
        bytes.data());
    LMX_ASSERT(buffer.has_value(), buffer.error().message);
    m_r10Ring[slot] = std::move(*buffer);
    m_creationCounters.bufferCreateCalls += 1;
    // Counted as per-frame binding delivery (spec section 9) when this recreation runs inside
    // runFrame()'s timed region; the three placeholder calls setup() makes are absorbed by
    // runFrame()'s own m_frameCounters reset before the first frame is ever reported.
    m_frameCounters.bufferCreateCalls += 1;
    m_frameCounters.bufferCreateBytes += workload::kEmissiveRingSlotSize;
}

//======================================================================================================================
// M5.1 Stage 4 binding-traffic instrumentation (RhiAdapter.h's header comment on this group): each
// forwards to the real rhi::CommandList call and bumps m_frameCounters by one, matching the
// prototype's own choke-point counting in Source/CommandBuffer.cpp.
void RhiAdapter::bindTextureCounted(rhi::CommandList& commands, uint32_t slot,
                                    rhi::Texture& texture, const rhi::TextureViewDesc& view) {
    commands.bindTexture(slot, texture, view);
    m_frameCounters.bindCalls += 1;
}

void RhiAdapter::bindBufferCounted(rhi::CommandList& commands, uint32_t slot, rhi::Buffer& buffer) {
    commands.bindBuffer(slot, buffer);
    m_frameCounters.bindCalls += 1;
}

void RhiAdapter::bindSamplerCounted(rhi::CommandList& commands, uint32_t slot,
                                    rhi::Sampler& sampler) {
    commands.bindSampler(slot, sampler);
    m_frameCounters.bindCalls += 1;
}

void RhiAdapter::bindStorageBufferCounted(rhi::CommandList& commands, uint32_t slot,
                                          rhi::Buffer& buffer, rhi::StorageAccess access) {
    commands.bindStorageBuffer(slot, buffer, access);
    m_frameCounters.bindCalls += 1;
}

void RhiAdapter::bindStorageTextureCounted(rhi::CommandList& commands, uint32_t slot,
                                           rhi::Texture& texture, const rhi::TextureViewDesc& view,
                                           rhi::StorageAccess access) {
    commands.bindStorageTexture(slot, texture, view, access);
    m_frameCounters.bindCalls += 1;
}

void RhiAdapter::setUniformsCounted(rhi::CommandList& commands, uint32_t slot, const void* data,
                                    uint64_t size) {
    commands.setUniforms(slot, data, size);
    m_frameCounters.setUniformsCalls += 1;
    m_frameCounters.setUniformsBytes += size;
}

//======================================================================================================================
void RhiAdapter::encodeFillHistogram(rhi::CommandList& commands) {
    commands.beginCopyPass("lmx.noapi.bench.fill.histogram");
    commands.fillBuffer(*m_r4Histogram, 0, workload::kHistogramBufferSize, 0);
    commands.endCopyPass();
}

//======================================================================================================================
void RhiAdapter::encodeUploadEmissive(rhi::CommandList& commands, uint32_t frameIndex) {
    rhi::Buffer& ring = *m_r10Ring[frameIndex % workload::kEmissiveRingSlotCount];
    commands.beginCopyPass("lmx.noapi.bench.upload.emissive");
    commands.copyBufferToTexture(
        ring, {.offset = 0, .bytesPerRow = uint64_t{workload::kMaterialTextureSize} * 4},
        *m_material0Emissive,
        {.mipLevel = 0,
         .arrayLayer = 0,
         .width = workload::kMaterialTextureSize,
         .height = workload::kMaterialTextureSize});
    commands.endCopyPass();
}

//======================================================================================================================
void RhiAdapter::encodeShadow(rhi::CommandList& commands) {
    commands.beginRenderPass({.colorTarget = nullptr,
                              .depthTarget = m_r1Shadow.get(),
                              .clearDepth = 0.0f,
                              .storeDepth = true,
                              .label = "lmx.noapi.bench.shadow"});
    commands.bindPipeline(*m_shadowPipeline);
    for (uint32_t draw = 0; draw < workload::kDrawCount; ++draw) {
        bindBufferCounted(commands, kVertexBufferSlot, *m_quadVertexBuffer);
        // Frame-invariant (this file's header comment's uniform-ring finding): built once in
        // createShadowObjectUniformBuffers().
        bindBufferCounted(commands, kObjectUniformsSlot, *m_shadowObjectUniformBuffers[draw]);
        commands.drawIndexed(*m_quadIndexBuffer, 6);
    }
    commands.endRenderPass();
}

//======================================================================================================================
void RhiAdapter::encodeScene(rhi::CommandList& commands, const glm::mat4& viewProj,
                             const glm::mat4& shadowTransform) {
    commands.beginRenderPass({.colorTarget = m_r2SceneColor.get(),
                              .clearColor = {0.0f, 0.0f, 0.0f, 1.0f},
                              .clear = true,
                              .depthTarget = m_r3SceneDepth.get(),
                              .clearDepth = 0.0f,
                              .storeDepth = false,
                              .label = "lmx.noapi.bench.scene"});
    commands.bindPipeline(*m_scenePipeline);
    bindSamplerCounted(commands, kLinearSamplerSlot, *m_linearSampler);
    bindSamplerCounted(commands, kShadowSamplerSlot, *m_shadowSampler);
    bindSamplerCounted(commands, kIblSamplerSlot, *m_iblSampler);
    bindTextureCounted(commands, kShadowTextureSlot, *m_r1Shadow);
    bindTextureCounted(commands, kIrradianceTextureSlot, *m_irradiance);
    bindTextureCounted(commands, kPrefilteredEnvTextureSlot, *m_prefilteredEnv);
    bindTextureCounted(commands, kDfgLutTextureSlot, *m_dfgLut);

    PassUniforms passUniforms{};
    passUniforms.viewProj = viewProj;
    passUniforms.shadowTransform = shadowTransform;
    passUniforms.eyePos = glm::vec3(0.0f);
    passUniforms.time = 0.0f;
    passUniforms.preExposure = 1.0f;
    passUniforms.lights[0] = {.strength = glm::vec3(3.0f), .direction = kLightDirection};
    passUniforms.lights[1] = {.strength = glm::vec3(0.0f), .direction = glm::vec3(0, -1, 0)};
    passUniforms.lights[2] = {.strength = glm::vec3(0.0f), .direction = glm::vec3(0, -1, 0)};
    passUniforms.shadowFilter = kShadowFilterPcf;
    setUniformsCounted(commands, kPassUniformsSlot, &passUniforms, sizeof(passUniforms));

    for (uint32_t draw = 0; draw < workload::kDrawCount; ++draw) {
        const uint32_t material = workload::drawMaterialIndex(draw);
        const workload::DrawMaterialParams params = workload::drawMaterialParams(draw);
        const workload::DrawPlacement placement = workload::drawPlacement(draw);
        const glm::mat4 model =
            glm::translate(glm::mat4(1.0f), glm::vec3(placement.x, 0.0f, placement.z));

        ObjectUniforms uniforms{};
        uniforms.mvp = viewProj * model;
        uniforms.model = model;
        uniforms.normalMatrix =
            glm::mat4(1.0f); // No rotation or non-uniform scale in this placement.
        uniforms.uvTransform = glm::mat4(1.0f);
        uniforms.albedo = glm::vec4(1.0f);
        uniforms.roughness = params.roughness;
        uniforms.flags = kFlagHasNormalMap;
        uniforms.metallic = params.metallic;
        uniforms.occlusionStrength = 1.0f;
        uniforms.emissive = glm::vec3(params.emissiveScale);

        const MaterialTextures& textures = m_materials[material];
        bindTextureCounted(commands, kDiffuseTextureSlot, *textures.baseColor);
        bindTextureCounted(commands, kNormalTextureSlot, *textures.normal);
        bindTextureCounted(commands, kMetallicRoughnessTextureSlot, *textures.metallicRoughness);
        bindTextureCounted(commands, kOcclusionTextureSlot, *textures.occlusion);
        bindTextureCounted(commands, kEmissiveTextureSlot,
                           material == 0 ? *m_material0Emissive : *textures.emissive);
        bindBufferCounted(commands, kVertexBufferSlot, *m_quadVertexBuffer);
        // The camera orbits every frame, so mvp -- and therefore the whole block -- is rebuilt and
        // reuploaded every frame (this file's header comment's uniform-ring finding); the previous
        // frame's buffer for this draw index is safe to replace because runFrame() waits the device
        // idle before the next frame ever records a command.
        auto buffer = m_device->createBuffer(
            {.size = sizeof(uniforms), .label = "lmx.noapi.bench.scene.objectUniforms"}, &uniforms);
        LMX_ASSERT(buffer.has_value(), buffer.error().message);
        m_sceneObjectUniformBuffers[draw] = std::move(*buffer);
        // Per-frame b1 buffer creation counted as binding delivery (spec section 8/9): this is the
        // adapter's only legal way to vary P04's per-draw uniforms every frame (this file's header
        // comment's uniform-ring finding), so it is binding traffic, not incidental setup.
        m_creationCounters.bufferCreateCalls += 1;
        m_frameCounters.bufferCreateCalls += 1;
        m_frameCounters.bufferCreateBytes += sizeof(uniforms);
        bindBufferCounted(commands, kObjectUniformsSlot, *m_sceneObjectUniformBuffers[draw]);
        commands.drawIndexed(*m_quadIndexBuffer, 6);
    }
    commands.endRenderPass();
}

//======================================================================================================================
void RhiAdapter::encodeHistogramAccumulate(rhi::CommandList& commands) {
    commands.beginComputePass("lmx.noapi.bench.histogram.accumulate");
    commands.bindComputePipeline(*m_histAccumulatePipeline);
    bindTextureCounted(commands, kHistTextureSlot, *m_r2SceneColor);
    bindBufferCounted(commands, kHistExposureSlot, *m_r5Exposure);
    bindStorageBufferCounted(commands, kHistBufferSlot, *m_r4Histogram,
                             rhi::StorageAccess::ReadWrite);
    const HistParams params{kExposureLogLuminanceMin, kExposureLogLuminanceMax};
    setUniformsCounted(commands, kHistParamsSlot, &params, sizeof(params));
    commands.dispatch(workload::kSceneWidth / 8, workload::kSceneHeight / 8, 1);
    commands.endComputePass();
}

//======================================================================================================================
void RhiAdapter::encodeHistogramResolve(rhi::CommandList& commands) {
    commands.beginComputePass("lmx.noapi.bench.histogram.resolve");
    commands.bindComputePipeline(*m_histResolvePipeline);
    bindStorageBufferCounted(commands, kHistBufferSlot, *m_r4Histogram, rhi::StorageAccess::Read);
    bindStorageBufferCounted(commands, kHistExposureSlot, *m_r5Exposure, rhi::StorageAccess::Write);
    const ResolveParams params{.lowPercentile = 50.0f,
                               .highPercentile = 95.0f,
                               .targetGrey = 0.18f,
                               .evMin = -8.0f,
                               .evMax = 8.0f,
                               .compensationEv = 0.0f,
                               .logLuminanceMin = kExposureLogLuminanceMin,
                               .logLuminanceMax = kExposureLogLuminanceMax};
    setUniformsCounted(commands, kHistParamsSlot, &params, sizeof(params));
    commands.dispatch(1, 1, 1);
    commands.endComputePass();
}

//======================================================================================================================
void RhiAdapter::encodeBloomThreshold(rhi::CommandList& commands) {
    commands.beginComputePass("lmx.noapi.bench.bloom.threshold");
    commands.bindComputePipeline(*m_bloomThresholdPipeline);
    bindTextureCounted(commands, kBloomThresholdSrcSlot, *m_r2SceneColor);
    bindStorageTextureCounted(commands, kBloomThresholdDstSlot, *m_r6BloomA,
                              rhi::TextureViewDesc{.range = mipRange(0)},
                              rhi::StorageAccess::Write);
    const ThresholdParams params{1.0f};
    setUniformsCounted(commands, kBloomThresholdParamsSlot, &params, sizeof(params));
    commands.dispatch(workload::kBloomExtent / 8, workload::kBloomExtent / 8, 1);
    commands.endComputePass();
}

//======================================================================================================================
void RhiAdapter::encodeBloomDown(rhi::CommandList& commands, rhi::Texture& target,
                                 const rhi::TextureSubresourceRange& srcRange, uint32_t srcExtent,
                                 const rhi::TextureSubresourceRange& dstRange, uint32_t dstExtent) {
    commands.beginComputePass("lmx.noapi.bench.bloom.down");
    commands.bindComputePipeline(*m_bloomDownPipeline);
    bindStorageTextureCounted(commands, kBloomDownSrcSlot, target,
                              rhi::TextureViewDesc{.range = srcRange}, rhi::StorageAccess::Read);
    bindStorageTextureCounted(commands, kBloomDownDstSlot, target,
                              rhi::TextureViewDesc{.range = dstRange}, rhi::StorageAccess::Write);
    const DownsampleParams params{srcExtent, srcExtent, dstExtent, dstExtent};
    setUniformsCounted(commands, kBloomDownParamsSlot, &params, sizeof(params));
    commands.dispatch((dstExtent + 7) / 8, (dstExtent + 7) / 8, 1);
    commands.endComputePass();
}

//======================================================================================================================
void RhiAdapter::encodeBloomUp(rhi::CommandList& commands, rhi::Texture& baseTexture,
                               const rhi::TextureSubresourceRange& baseRange,
                               rhi::Texture& smallTexture,
                               const rhi::TextureSubresourceRange& smallRange, uint32_t smallExtent,
                               rhi::Texture& dstTexture,
                               const rhi::TextureSubresourceRange& dstRange, uint32_t dstExtent) {
    commands.beginComputePass("lmx.noapi.bench.bloom.up");
    commands.bindComputePipeline(*m_bloomUpPipeline);
    bindStorageTextureCounted(commands, kBloomUpBaseSlot, baseTexture,
                              rhi::TextureViewDesc{.range = baseRange}, rhi::StorageAccess::Read);
    bindStorageTextureCounted(commands, kBloomUpSmallSlot, smallTexture,
                              rhi::TextureViewDesc{.range = smallRange}, rhi::StorageAccess::Read);
    bindStorageTextureCounted(commands, kBloomUpDstSlot, dstTexture,
                              rhi::TextureViewDesc{.range = dstRange}, rhi::StorageAccess::Write);
    const UpsampleParams params{smallExtent, smallExtent, dstExtent, dstExtent};
    setUniformsCounted(commands, kBloomUpParamsSlot, &params, sizeof(params));
    commands.dispatch((dstExtent + 7) / 8, (dstExtent + 7) / 8, 1);
    commands.endComputePass();
}

//======================================================================================================================
void RhiAdapter::encodeComposite(rhi::CommandList& commands) {
    commands.beginRenderPass({.colorTarget = m_r8Out.get(),
                              .clearColor = {0.0f, 0.0f, 0.0f, 1.0f},
                              .clear = true,
                              .depthTarget = nullptr,
                              .label = "lmx.noapi.bench.composite"});
    commands.bindPipeline(*m_compositePipeline);
    bindTextureCounted(commands, kCompositeSceneSlot, *m_r2SceneColor);
    bindTextureCounted(commands, kCompositeBloomSlot, *m_r7BloomB);
    bindBufferCounted(commands, kCompositeExposureSlot, *m_r5Exposure);
    const CompositeParams params{0.2f};
    setUniformsCounted(commands, kCompositeParamsSlot, &params, sizeof(params));
    commands.draw(3);
    commands.endRenderPass();
}

//======================================================================================================================
void RhiAdapter::encodeReadback(rhi::CommandList& commands) {
    commands.beginCopyPass("lmx.noapi.bench.readback");
    commands.copyTextureToBuffer(*m_r8Out,
                                 {.mipLevel = 0,
                                  .arrayLayer = 0,
                                  .width = workload::kSceneWidth,
                                  .height = workload::kSceneHeight},
                                 *m_r9Readback,
                                 {.offset = 0, .bytesPerRow = uint64_t{workload::kSceneWidth} * 4});
    if (m_diagScene) {
        // Unscored diagnostic: the P13 barrier already ordered the scene pass's fragment writes
        // before this copy pass, so R2 is readable here without a barrier of its own.
        commands.copyTextureToBuffer(
            *m_r2SceneColor,
            {.mipLevel = 0,
             .arrayLayer = 0,
             .width = workload::kSceneWidth,
             .height = workload::kSceneHeight},
            *m_diagSceneColor, {.offset = 0, .bytesPerRow = uint64_t{workload::kSceneWidth} * 8});
    }
    commands.endCopyPass();
}

//======================================================================================================================
void RhiAdapter::runFrame(uint32_t frameIndex, std::vector<uint8_t>& outReadback) {
    m_frameCounters = FrameBindingCounters{};

    // ---- BEGIN TIMED REGION -----------------------------------------------------------------
    // M5.1 Stage 4 (spec section 8): std::chrono::steady_clock on both adapters, identically. The
    // production RHI bundles the frame-slot pacing wait inside beginFrame() itself with no separate
    // hook to time around, so this adapter's clock necessarily starts at the top of the call that
    // performs it -- see this file's header comment and the M5.1 evidence document for why that is
    // an accepted, reported asymmetry rather than a silent one: this bench's own waitIdle() at the
    // end of every frame (below, outside the region) means the pacing wait it bundles is always
    // trivially satisfied, so its cost inside the timed sample is negligible in practice.
    const auto timedRegionStart = std::chrono::steady_clock::now();
    rhi::CommandList& commands = m_device->beginFrame();

    // Stands in for a per-frame ring rewrite (this file's header comment); moved here, after
    // beginFrame(), to match that comment's own contract ("runs inside the timed region ... between
    // beginFrame() and endFrame()") -- it previously ran before beginFrame(), outside the region
    // the file's own documentation already claimed for it.
    refreshEmissiveStaging(frameIndex);

    const workload::CameraPose camera = workload::cameraForFrame(frameIndex);
    const glm::mat4 view = glm::lookAt(glm::vec3(camera.eyeX, camera.eyeY, camera.eyeZ),
                                       glm::vec3(camera.targetX, camera.targetY, camera.targetZ),
                                       glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 proj = reversedInfiniteFarProjection(glm::radians(60.0f), 1.0f, 0.1f);
    const glm::mat4 viewProj = proj * view;
    const ShadowMatrices shadow =
        fitShadowOrtho(glm::vec4(0.0f, 0.0f, 0.0f, kBoundingRadius), kLightDirection);

    const auto emitBarriers = [&](size_t passIndex) {
        for (const BarrierOp& op : m_barriersBeforePass[passIndex]) {
            if (const auto* textureOp = std::get_if<TextureBarrierOp>(&op)) {
                commands.textureBarrier(*textureOp->texture, textureOp->range, textureOp->from,
                                        textureOp->to);
            } else {
                const auto& bufferOp = std::get<BufferBarrierOp>(op);
                commands.bufferBarrier(*bufferOp.buffer, bufferOp.from, bufferOp.to);
            }
            m_frameCounters.barrierCalls += 1;
        }
    };

    emitBarriers(0);
    encodeFillHistogram(commands); // P01
    emitBarriers(1);
    encodeUploadEmissive(commands, frameIndex); // P02
    emitBarriers(2);
    encodeShadow(commands); // P03
    emitBarriers(3);
    encodeScene(commands, viewProj, shadow.shadowTransform); // P04
    emitBarriers(4);
    encodeHistogramAccumulate(commands); // P05
    emitBarriers(5);
    encodeHistogramResolve(commands); // P06
    emitBarriers(6);
    encodeBloomThreshold(commands); // P07
    emitBarriers(7);
    encodeBloomDown(commands, *m_r6BloomA, mipRange(0), workload::kBloomExtent, mipRange(1),
                    workload::kBloomExtent / 2); // P08
    emitBarriers(8);
    encodeBloomDown(commands, *m_r6BloomA, mipRange(1), workload::kBloomExtent / 2, mipRange(2),
                    workload::kBloomExtent / 4); // P09
    emitBarriers(9);
    encodeBloomUp(commands, *m_r6BloomA, mipRange(1), *m_r6BloomA, mipRange(2),
                  workload::kBloomExtent / 4, *m_r7BloomB, mipRange(1),
                  workload::kBloomExtent / 2); // P10
    emitBarriers(10);
    encodeBloomUp(commands, *m_r6BloomA, mipRange(0), *m_r7BloomB, mipRange(1),
                  workload::kBloomExtent / 2, *m_r7BloomB, mipRange(0),
                  workload::kBloomExtent); // P11
    emitBarriers(11);
    encodeComposite(commands); // P12
    emitBarriers(12);
    encodeReadback(commands); // P13

    m_device->endFrame(nullptr);
    const auto timedRegionEnd = std::chrono::steady_clock::now();
    // ---- END TIMED REGION -------------------------------------------------------------------

    m_lastFrameTimedRegionNs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(timedRegionEnd - timedRegionStart)
            .count());
    m_lastFrameCounters = m_frameCounters;

    m_device->waitIdle();
    if (m_diagScene) {
        outReadback.resize(m_diagSceneColor->size());
        m_diagSceneColor->readback(outReadback.data(), outReadback.size());
        return;
    }
    outReadback.resize(workload::kReadbackBufferSize);
    m_r9Readback->readback(outReadback.data(), outReadback.size());
}

//======================================================================================================================
AllocationSnapshot RhiAdapter::allocationSnapshot() const {
    if (m_device == nullptr) {
        return {};
    }
    AllocationSnapshot snapshot{
        .textureCreateCalls = static_cast<uint32_t>(m_creationCounters.textureCreateCalls),
        .bufferCreateCalls = static_cast<uint32_t>(m_creationCounters.bufferCreateCalls),
        .samplerCreateCalls = static_cast<uint32_t>(m_creationCounters.samplerCreateCalls),
        .pipelineCreateCalls = static_cast<uint32_t>(m_creationCounters.pipelineCreateCalls),
        .residentBytesIsMetalReported = false,
    };

    uint64_t bytes = 0;
    const auto addTexture = [&](const rhi::Texture* texture) {
        if (texture != nullptr) {
            bytes += textureRequestedBytes(*texture);
        }
    };
    const auto addBuffer = [&](const rhi::Buffer* buffer) {
        if (buffer != nullptr) {
            bytes += buffer->size();
        }
    };

    for (const rhi::Texture* texture :
         {m_r1Shadow.get(), m_r2SceneColor.get(), m_r3SceneDepth.get(), m_r6BloomA.get(),
          m_r7BloomB.get(), m_r8Out.get(), m_material0Emissive.get(), m_irradiance.get(),
          m_prefilteredEnv.get(), m_dfgLut.get()}) {
        addTexture(texture);
    }
    for (const MaterialTextures& material : m_materials) {
        addTexture(material.baseColor.get());
        addTexture(material.normal.get());
        addTexture(material.metallicRoughness.get());
        addTexture(material.occlusion.get());
        addTexture(material.emissive.get());
    }
    addBuffer(m_r4Histogram.get());
    addBuffer(m_r5Exposure.get());
    addBuffer(m_r9Readback.get());
    addBuffer(m_quadVertexBuffer.get());
    addBuffer(m_quadIndexBuffer.get());
    for (const auto& ring : m_r10Ring) {
        addBuffer(ring.get());
    }
    for (const auto& buffer : m_shadowObjectUniformBuffers) {
        addBuffer(buffer.get());
    }
    for (const auto& buffer : m_sceneObjectUniformBuffers) {
        addBuffer(buffer.get());
    }
    if (m_diagScene) {
        addBuffer(m_diagSceneColor.get());
    }

    snapshot.residentBytes = bytes;
    return snapshot;
}

//======================================================================================================================
void RhiAdapter::teardown() {
    if (m_device) {
        m_device->waitIdle();
    }
}

} // namespace lmx::noapi::bench
