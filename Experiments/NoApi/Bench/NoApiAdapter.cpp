//----------------------------------------------------------------------------------------------------------------------
/// @file NoApiAdapter.cpp
/// @brief Implements the address-first prototype adapter.
///
/// Per-frame work placement against the spec section 8 timed region, stated on exactly the terms
/// RhiAdapter.cpp states its own:
///
///   BEFORE the timed region (setup(), called once): device, residency set, bindless table, and
///   frame-ring creation; every memory allocation; every texture creation and its one-time upload
///   (the 64 material sets, the IBL set, the shared quad, material 0's emissive base content);
///   every bindless slot write; every pipeline compile; the one-time write of every frame-invariant
///   root block (writePersistentRoots(), see the placement note below); and the one-time barrier
///   derivation (planBarriers()) that walks the manifest's declared resource uses and reduces them
///   to one producer/consumer stage pair per pass boundary -- the planner runs exactly once here,
///   never per frame (spec section 8: "the graph planner must NOT run per-frame or inside any timed
///   region; the schedule is data from the manifest"). The setup upload command buffer is submitted
///   and waited on here, and the staging allocation it used is released before setup returns, so it
///   never counts toward the resident-bytes dimension of a measured frame.
///
///   INSIDE the timed region (runFrame(), between beginCommands() and the FrameRing endFrame()
///   submit): this frame's R10 ring-slot write, the camera and shadow matrix evaluation, the fixed
///   P01-P13 replay -- every barrier() call, every setPipeline/setViewport-class state call, the
///   1,025 pushRoot() calls this frame's data actually requires (1,024 P04 per-draw blocks plus the
///   one P04 pass block), every draw/dispatch/copy, and the submit. Every `setAddress` the
///   interface makes is inside the region whether the address names ring storage or persistent
///   storage, so nothing about the binding traffic is hidden by the split below.
///
///   PLACEMENT NOTE -- what is pushed per frame is exactly what varies per frame, decided field by
///   field rather than by pass:
///     * P03's 1,024 per-draw blocks are frame-invariant. Each is `lightViewProj * model`, and both
///       factors are fixed for the whole run -- the shadow light and its fitted ortho frustum never
///       move, and object transforms are static (spec section 6). They are written once, into one
///       contiguous persistent array, and each draw passes `m_shadowRootBase + i * 80`. This is the
///       address-first model's own answer to invariant per-draw data: no ring slot, no rewrite, and
///       no three-frames-in-flight lifetime rule, because nothing ever writes the storage again.
///       It also matches where RhiAdapter.cpp puts the same data (its shadow uniform buffers are
///       built once in setup()), so neither side is charged per-frame cost for constants.
///     * P05-P12's eight kernel and composite blocks are frame-invariant too: they carry slot
///       indices, the bindless table's address, the histogram and exposure addresses, and the
///       frozen tuning constants, none of which depend on the frame. All eight are written once
///       alongside P03's.
///     * P04's 1,024 per-draw blocks are not invariant -- the camera orbits, so every `mvp` changes
///       -- and neither is its pass block, whose `viewProj` changes with the same camera. Those
///       1,025 blocks are pushed into the frame ring every frame, inside the timed region. The pass
///       block is pushed whole rather than split into varying and invariant halves because its
///       layout mirrors the production `PassUniforms` ABI the workload freezes.
///     * R10's ring-slot write is per frame by definition (spec section 6: the staging content is a
///       function of frame index).
///
///   PLACEMENT NOTE -- the R10 ring-slot write is a plain CPU store through the allocation's mapped
///   pointer and is inside the timed region, matching where RhiAdapter.cpp's header comment places
///   its own (necessarily heavier) stand-in for the same work. There is no buffer object to
///   recreate here: `Allocation::cpu` is a write-combined pointer into memory the GPU already
///   addresses, so the "upload" is a memcpy plus the copy command P02 records.
///
///   OUTSIDE the timed region, after it (runFrame(), after endFrame()): the wait for the frame to
///   retire and the R9 readback. Both are explicitly excluded by spec section 8 ("excludes pacing
///   waits, ... and readback waits"). The wait polls `FrameRing::isSlotRetired` rather than
///   blocking on a semaphore value, because `FrameRing` owns its pacing semaphore privately and
///   this interface exposes no second signal on the same submission; a correctness run reads back
///   every frame, so this stands where RhiAdapter.cpp's `Device::waitIdle()` stands, with the same
///   effect (every frame fully retired before the next records) and the same exclusion from the
///   region.
///
///   FINDING -- the bindless table is reached from root data, not from a bound table index. The
///   prototype publishes the table's address at argument-table index 1 (`setBindlessTable`, one
///   `setAddress` per command buffer) and this adapter calls it, but no shader reads index 1: a
///   Metal buffer binding carries exactly one shader-side element type, and this workload needs
///   five simultaneous views of the one table (2D texture, cube, storage image, sampler, comparison
///   sampler). Each root block therefore carries the table's address once per view type it needs.
///   That is more faithful to the model, not less -- the table is memory, and the shader indexes it
///   from an address -- but it means the published index-1 binding is pure overhead on this target,
///   counted against the prototype rather than dropped. Recorded in
///   docs/research/2026-08-12-execution-model-evidence.md section 5.10.
//----------------------------------------------------------------------------------------------------------------------

#include "Bench/NoApiAdapter.h"

#include "Workload/AssetGen.h"
#include "Workload/DrawPolicy.h"
#include "Workload/ProdValues.h"

#include "Core/Assert.h"
#include "Render/Renderer.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <span>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace lmx::noapi::bench {
namespace {

namespace workload = lmx::noapi::workload;

//======================================================================================================================
Format mapFormat(workload::Format format) {
    switch (format) {
    case workload::Format::RGBA8Unorm:
        return Format::RGBA8Unorm;
    case workload::Format::RGBA8Unorm_sRGB:
        return Format::RGBA8UnormSrgb;
    case workload::Format::RGBA16Float:
        return Format::RGBA16Float;
    case workload::Format::RG16Float:
        return Format::RG16Float;
    case workload::Format::D32Float:
        return Format::D32Float;
    }
    LMX_ASSERT(false, "NoApiAdapter: unhandled workload::Format");
    return Format::Undefined;
}

// Fixed creation order of every texture this adapter owns. The bindless slot map below is derived
// from the same order, so a texture's index and its slot are two views of one table.
constexpr size_t kTexShadow = 0;       // R1
constexpr size_t kTexSceneColor = 1;   // R2
constexpr size_t kTexSceneDepth = 2;   // R3
constexpr size_t kTexBloomA = 3;       // R6
constexpr size_t kTexBloomB = 4;       // R7
constexpr size_t kTexOut = 5;          // R8
constexpr size_t kTexMaterialBase = 6; // 64 materials x 5 slots
constexpr uint32_t kMaterialSlotCount = 5;
constexpr size_t kTexIrradiance = kTexMaterialBase + workload::kMaterialCount * kMaterialSlotCount;
constexpr size_t kTexPrefiltered = kTexIrradiance + 1;
constexpr size_t kTexDfgLut = kTexPrefiltered + 1;
constexpr size_t kTextureCount = kTexDfgLut + 1;

// Index of material 0's emissive texture: the one Materials texture P02 writes every frame, tracked
// by the manifest as its own graph resource (RepresentativeGraph.h's kMaterialZeroEmissiveId).
constexpr size_t kTexMaterialZeroEmissive = kTexMaterialBase + 4;

// The bindless table's fixed slot map. Materials come first so a draw's five slots are
// `material * 5 + role`, which is the whole per-draw texture "binding" this adapter performs.
constexpr uint32_t kSlotMaterialBase = 0;
constexpr uint32_t kSlotShadow = workload::kMaterialCount * kMaterialSlotCount;
constexpr uint32_t kSlotIrradiance = kSlotShadow + 1;
constexpr uint32_t kSlotPrefiltered = kSlotIrradiance + 1;
constexpr uint32_t kSlotDfgLut = kSlotPrefiltered + 1;
constexpr uint32_t kSlotLinearSampler = kSlotDfgLut + 1;
constexpr uint32_t kSlotShadowSampler = kSlotLinearSampler + 1;
constexpr uint32_t kSlotIblSampler = kSlotShadowSampler + 1;
constexpr uint32_t kSlotSceneColor = kSlotIblSampler + 1;
constexpr uint32_t kSlotBloomAMip = kSlotSceneColor + 1;                       // + mip level
constexpr uint32_t kSlotBloomBMip = kSlotBloomAMip + workload::kBloomMipCount; // + mip level
constexpr uint32_t kSlotBloomBSampled = kSlotBloomBMip + workload::kBloomMipCount;
constexpr uint32_t kBindlessSlotCount = kSlotBloomBSampled + 1;

// Order of a material's five texture roles inside its slot run, matching AssetGen's
// MaterialTextureSlot enumerators (whose values are ScenePass register numbers, not indices).
constexpr std::array<workload::MaterialTextureSlot, kMaterialSlotCount> kMaterialRoles{
    workload::MaterialTextureSlot::BaseColor, workload::MaterialTextureSlot::Normal,
    workload::MaterialTextureSlot::MetallicRoughness, workload::MaterialTextureSlot::Occlusion,
    workload::MaterialTextureSlot::Emissive};

constexpr uint32_t kMaterialMipCount = 7; // 64, 32, 16, 8, 4, 2, 1

// Bump-allocation granularity for staging suballocations: Metal's buffer-to-texture copies want the
// source offset comfortably aligned, and 256 is the alignment the production backend uses for its
// own uniform slices.
constexpr uint64_t kStagingAlignment = 256;

//======================================================================================================================
// Shader-visible root blocks. Each is the CPU half of the identically-named Slang declaration in
// Experiments/NoApi/Shaders; the two declarations are the whole layout contract, and the
// static_asserts below pin the offsets Slang's emitted MSL was read to produce (16-byte-sized
// members first, eight-byte addresses last, so the constant-buffer and natural layouts agree).
struct alignas(16) ShadowObjectRoot {
    glm::mat4 mvp;
    uint64_t vertices;
    uint32_t padding[2];
};
static_assert(sizeof(ShadowObjectRoot) == 80);
static_assert(offsetof(ShadowObjectRoot, vertices) == 64);

struct alignas(16) ScenePassRoot {
    glm::mat4 viewProj;
    glm::mat4 shadowTransform;
    glm::vec4 eyePositionAndTime;
    glm::vec4 preExposure;
    glm::vec4 lightStrength0;
    glm::vec4 lightDirection0;
    glm::vec4 lightStrength1;
    glm::vec4 lightDirection1;
    glm::vec4 lightStrength2;
    glm::vec4 lightDirection2;
    glm::uvec4 sharedSlots;
    glm::uvec4 samplerSlots;
    uint64_t textures;
    uint64_t cubes;
    uint64_t samplers;
    uint64_t comparisonSamplers;
};
static_assert(sizeof(ScenePassRoot) == 320);
static_assert(offsetof(ScenePassRoot, sharedSlots) == 256);
static_assert(offsetof(ScenePassRoot, textures) == 288);

struct alignas(16) SceneObjectRoot {
    glm::mat4 mvp;
    glm::mat4 model;
    glm::mat4 normalMatrix;
    glm::mat4 uvTransform;
    glm::vec4 albedo;
    glm::vec4 materialParams;
    glm::vec4 emissive;
    glm::uvec4 flagsAndSlots;
    glm::uvec4 materialSlots;
    uint64_t vertices;
    uint32_t padding[2];
};
static_assert(sizeof(SceneObjectRoot) == 352);
static_assert(offsetof(SceneObjectRoot, albedo) == 256);
static_assert(offsetof(SceneObjectRoot, vertices) == 336);

struct alignas(16) HistAccumulateRoot {
    glm::uvec4 slots;
    glm::vec4 range;
    uint64_t textures;
    uint64_t exposure;
    uint64_t histogram;
    uint32_t padding[2];
};
static_assert(sizeof(HistAccumulateRoot) == 64);

struct alignas(16) HistResolveRoot {
    glm::vec4 percentiles;
    glm::vec4 evRange;
    uint64_t histogram;
    uint64_t exposure;
};
static_assert(sizeof(HistResolveRoot) == 48);

struct alignas(16) BloomThresholdRoot {
    glm::uvec4 slots;
    glm::vec4 params;
    uint64_t textures;
    uint64_t images;
};
static_assert(sizeof(BloomThresholdRoot) == 48);

struct alignas(16) BloomDownRoot {
    glm::uvec4 extents;
    glm::uvec4 slots;
    uint64_t images;
    uint32_t padding[2];
};
static_assert(sizeof(BloomDownRoot) == 48);

struct alignas(16) BloomUpRoot {
    glm::uvec4 extents;
    glm::uvec4 slots;
    uint64_t images;
    uint32_t padding[2];
};
static_assert(sizeof(BloomUpRoot) == 48);

struct alignas(16) CompositeRoot {
    glm::uvec4 slots;
    glm::vec4 params;
    uint64_t textures;
    uint64_t exposure;
};
static_assert(sizeof(CompositeRoot) == 48);

//======================================================================================================================
// Reversed infinite-far perspective for Metal's [0,1] clip depth, restated from
// Render/Camera.cpp::projectionMatrix exactly as RhiAdapter.cpp restates it: the two adapters must
// feed their pipelines the same matrix, bit for bit.
glm::mat4 reversedInfiniteFarProjection(float fovYRadians, float aspect, float nearZ) {
    const float tanHalfFovY = std::tan(fovYRadians * 0.5f);
    glm::mat4 projection{0.0f};
    projection[0][0] = 1.0f / (aspect * tanHalfFovY);
    projection[1][1] = 1.0f / tanHalfFovY;
    projection[2][3] = -1.0f;
    projection[3][2] = nearZ;
    return projection;
}

// Half the frozen 32x32 draw grid's diagonal plus the quad's own half-extent, restated from
// RhiAdapter.cpp so both adapters fit the same shadow frustum.
constexpr float kGridHalfExtent = (workload::kDrawGridSize - 1) * 0.5f;
constexpr float kBoundingRadius = kGridHalfExtent * 1.5f + 1.0f;

const glm::vec3 kLightDirection = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));

// Source/Render/Renderer.cpp's shipped shadow depth bias, which the production shadow pipeline
// bakes and the incumbent adapter reuses; the prototype sets it as render-encoder state instead.
constexpr float kShadowDepthBiasConstant = -4.0f;
constexpr float kShadowDepthBiasSlopeScale = -32.0f;

constexpr uint32_t kFlagHasNormalMap = 1u;
constexpr uint32_t kShadowFilterPcf = 0;

constexpr float kExposureLogLuminanceMin = -12.0f;
constexpr float kExposureLogLuminanceMax = 4.0f;
constexpr float kBloomThreshold = 1.0f;
constexpr float kBloomIntensity = 0.2f;

// Root-ring capacity: a frame pushes 1,024 x 352-byte scene blocks plus one 320-byte pass block,
// just over 352 KiB, so half a MiB per slot leaves the ring comfortably rather than marginally
// sufficient without inflating the prototype's own resident bytes.
constexpr uint64_t kRootBytesPerFrame = 512ull << 10;

// Persistent root storage: P03's 1,024 frame-invariant per-draw blocks as one contiguous array,
// plus the eight frame-invariant P05-P12 blocks with room for each to align to 16 bytes.
constexpr uint64_t kPersistentRootBytes =
    uint64_t{workload::kDrawCount} * sizeof(ShadowObjectRoot) + 8 * 64;

//======================================================================================================================
// Reads a whole file, preferring a precompiled `.metallib` when the optional Metal toolchain
// produced one and falling back to the `.metal` source the slang2metallib rule always emits;
// createComputePipeline/createGraphicsPipeline tell the two apart by the library magic.
bool readShader(std::string_view name, std::vector<std::byte>& out) {
    const std::filesystem::path base = std::filesystem::path("Shaders") / std::string(name);
    for (const char* extension : {".metallib", ".metal"}) {
        std::filesystem::path path = base;
        path += extension;
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            continue;
        }
        const std::streamoff size = file.tellg();
        file.seekg(0);
        out.resize(static_cast<size_t>(size));
        file.read(reinterpret_cast<char*>(out.data()), size);
        if (!file) {
            continue;
        }
        return true;
    }
    std::cerr << "NoApiAdapter: failed to read shader '" << name << "' (looked for Shaders/" << name
              << ".metallib and .metal relative to the working "
              << "directory; run NoApiBench from its own build directory)\n";
    return false;
}

//======================================================================================================================
std::span<const std::byte> asCode(const std::vector<std::byte>& bytes) {
    return {bytes.data(), bytes.size()};
}

} // namespace

//======================================================================================================================
NoApiAdapter::~NoApiAdapter() {
    teardown();
}

//======================================================================================================================
bool NoApiAdapter::setup() {
    m_diagScene = std::getenv("LMX_NOAPI_DIAG_SCENE") != nullptr;

    Result<Device*> device = createDevice({.label = "lmx.noapi.bench.proto"});
    if (!device) {
        std::cerr << "NoApiAdapter::setup: createDevice failed: " << device.error().message << "\n";
        return false;
    }
    m_device = *device;
    m_queue = mainQueue(m_device);

    Result<ResidencySet*> residency = createResidencySet(
        m_device, {.initialCapacity = 8, .label = "lmx.noapi.bench.proto.residency"});
    if (!residency) {
        std::cerr << "NoApiAdapter::setup: " << residency.error().message << "\n";
        return false;
    }
    m_residency = *residency;

    if (!createMemory() || !createTextures() || !createSamplersAndTable() || !createPipelines()) {
        return false;
    }
    writePersistentRoots();

    Result<FrameRing> ring = FrameRing::create(
        m_device, {.bytesPerFrame = kRootBytesPerFrame, .label = "lmx.noapi.bench.proto.rootRing"});
    if (!ring) {
        std::cerr << "NoApiAdapter::setup: " << ring.error().message << "\n";
        return false;
    }
    m_ring = std::move(*ring);

    commitResidency(m_residency);

    if (!uploadStaticAssets()) {
        return false;
    }
    planBarriers();

    // Every bindless table write this adapter ever makes happens above, one time, before the first
    // runFrame() call (this file's header comment's PLACEMENT NOTEs: every root block and table
    // slot this manifest needs is frame-invariant). Sampling the table's cumulative counters here
    // is what lets runFrame() report a per-frame delta of exactly zero rather than the whole run's
    // total.
    const BindlessTableStats tableStats = bindlessTableStats(m_table);
    m_tableWriteCallsAtLastSample = tableStats.writeCalls;
    m_tableWriteBytesAtLastSample = tableStats.writeBytes;
    return true;
}

//======================================================================================================================
bool NoApiAdapter::createMemory() {
    const auto allocateOrLog = [&](uint64_t size, uint64_t alignment, MemoryKind kind,
                                   std::string_view label, Allocation& out) {
        Result<Allocation> allocation = allocate(
            m_device, {.size = size, .alignment = alignment, .kind = kind, .label = label});
        if (!allocation) {
            std::cerr << "NoApiAdapter: " << allocation.error().message << "\n";
            return false;
        }
        out = *allocation;
        return true;
    };

    // Setup-only staging, sized for every mip of every synthetic asset plus its 256-byte alignment
    // padding; released in uploadStaticAssets() once the upload submission has retired.
    if (!allocateOrLog(16ull << 20, kStagingAlignment, MemoryKind::Shared,
                       "lmx.noapi.bench.proto.staging", m_staging)) {
        return false;
    }
    m_stagingAllocator = LinearAllocator(m_staging);

    const workload::QuadGeometry quad = workload::sharedQuad();
    if (!allocateOrLog(sizeof(quad.vertices) + sizeof(quad.indices), 256, MemoryKind::Shared,
                       "lmx.noapi.bench.proto.quad", m_geometry)) {
        return false;
    }
    m_quadVertexAddress = m_geometry.gpu;
    m_quadIndexAddress = m_geometry.gpu + sizeof(quad.vertices);
    std::memcpy(m_geometry.cpu, quad.vertices.data(), sizeof(quad.vertices));
    std::memcpy(static_cast<uint8_t*>(m_geometry.cpu) + sizeof(quad.vertices), quad.indices.data(),
                sizeof(quad.indices));

    // R4 (histogram) and R5 (exposure) share one allocation: both are plain GPU memory the shaders
    // reach by address, so "two buffers" is two offsets rather than two objects.
    if (!allocateOrLog(4096, 256, MemoryKind::Shared, "lmx.noapi.bench.proto.storage",
                       m_storageBuffers)) {
        return false;
    }
    m_histogramAddress = m_storageBuffers.gpu;
    m_exposureAddress = m_storageBuffers.gpu + workload::kHistogramBufferSize;
    std::memset(m_storageBuffers.cpu, 0, workload::kHistogramBufferSize);
    // The incumbent creates R5 with an initial 1.0; frame 0's P05 reads that value before any
    // resolve has run, so it is part of the frozen output.
    const float initialExposure = 1.0f;
    std::memcpy(static_cast<uint8_t*>(m_storageBuffers.cpu) + workload::kHistogramBufferSize,
                &initialExposure, sizeof(initialExposure));

    if (!allocateOrLog(workload::kEmissiveRingSlotSize * workload::kEmissiveRingSlotCount, 256,
                       MemoryKind::Shared, "lmx.noapi.bench.proto.emissiveRing", m_emissiveRing)) {
        return false;
    }
    if (!allocateOrLog(kPersistentRootBytes, 256, MemoryKind::Shared,
                       "lmx.noapi.bench.proto.persistentRoots", m_persistentRoots)) {
        return false;
    }
    if (!allocateOrLog(workload::kReadbackBufferSize, 256, MemoryKind::Readback,
                       "lmx.noapi.bench.proto.readback", m_readback)) {
        return false;
    }
    if (m_diagScene &&
        !allocateOrLog(uint64_t{workload::kSceneWidth} * workload::kSceneHeight * 8, 256,
                       MemoryKind::Readback, "lmx.noapi.bench.diag.sceneColor", m_diagSceneColor)) {
        return false;
    }
    return true;
}

//======================================================================================================================
bool NoApiAdapter::createTextures() {
    // Descriptors are built first, in the fixed kTex* order, so the private allocation can be sized
    // from exactly what will be placed in it rather than from a guess.
    std::vector<TextureDesc> descs;
    descs.reserve(kTextureCount);
    const auto label = [&](std::string text) -> std::string_view {
        m_textureLabels.push_back(std::move(text));
        return m_textureLabels.back();
    };

    descs.push_back({.kind = TextureKind::Texture2D,
                     .extent = {.width = workload::kShadowExtent,
                                .height = workload::kShadowExtent,
                                .depth = 1},
                     .format = Format::D32Float,
                     .usage = TextureUsage::DepthStencilAttachment | TextureUsage::Sampled,
                     .label = label("lmx.noapi.bench.proto.R1.shadow")});
    descs.push_back(
        {.kind = TextureKind::Texture2D,
         .extent = {.width = workload::kSceneWidth, .height = workload::kSceneHeight, .depth = 1},
         .format = Format::RGBA16Float,
         // CopySource is declared unconditionally so the unscored LMX_NOAPI_DIAG_SCENE readback
         // needs no second descriptor: it maps to no Metal usage bit at all (Source/Texture.cpp's
         // toMTL), so declaring it cannot perturb what the scored path renders.
         .usage = TextureUsage::ColorAttachment | TextureUsage::Sampled | TextureUsage::CopySource,
         .label = label("lmx.noapi.bench.proto.R2.sceneColor")});
    descs.push_back(
        {.kind = TextureKind::Texture2D,
         .extent = {.width = workload::kSceneWidth, .height = workload::kSceneHeight, .depth = 1},
         .format = Format::D32Float,
         .usage = TextureUsage::DepthStencilAttachment,
         .label = label("lmx.noapi.bench.proto.R3.sceneDepth")});
    for (const char* bloom : {"R6.bloomA", "R7.bloomB"}) {
        descs.push_back({.kind = TextureKind::Texture2D,
                         .extent = {.width = workload::kBloomExtent,
                                    .height = workload::kBloomExtent,
                                    .depth = 1},
                         .mipCount = workload::kBloomMipCount,
                         .format = Format::RGBA16Float,
                         .usage = TextureUsage::Storage | TextureUsage::Sampled,
                         .label = label(std::string("lmx.noapi.bench.proto.") + bloom)});
    }
    descs.push_back(
        {.kind = TextureKind::Texture2D,
         .extent = {.width = workload::kSceneWidth, .height = workload::kSceneHeight, .depth = 1},
         .format = Format::RGBA8Unorm,
         .usage = TextureUsage::ColorAttachment | TextureUsage::CopySource,
         .label = label("lmx.noapi.bench.proto.R8.out")});

    for (uint32_t material = 0; material < workload::kMaterialCount; ++material) {
        for (uint32_t role = 0; role < kMaterialSlotCount; ++role) {
            const workload::Format format = [&] {
                switch (kMaterialRoles[role]) {
                case workload::MaterialTextureSlot::BaseColor:
                    return workload::prod::kBaseColorFormat;
                case workload::MaterialTextureSlot::Normal:
                    return workload::prod::kNormalFormat;
                case workload::MaterialTextureSlot::MetallicRoughness:
                    return workload::prod::kMetallicRoughnessFormat;
                case workload::MaterialTextureSlot::Occlusion:
                    return workload::prod::kOcclusionFormat;
                case workload::MaterialTextureSlot::Emissive:
                    break;
                }
                return workload::prod::kEmissiveFormat;
            }();
            descs.push_back({.kind = TextureKind::Texture2D,
                             .extent = {.width = workload::kMaterialTextureSize,
                                        .height = workload::kMaterialTextureSize,
                                        .depth = 1},
                             .mipCount = kMaterialMipCount,
                             .format = mapFormat(format),
                             .usage = TextureUsage::Sampled | TextureUsage::CopyDestination,
                             .label = label(std::format("lmx.noapi.bench.proto.material{}.{}",
                                                        material, role))});
        }
    }

    descs.push_back({.kind = TextureKind::TextureCube,
                     .extent = {.width = workload::prod::kIrradianceFaceSize,
                                .height = workload::prod::kIrradianceFaceSize,
                                .depth = 1},
                     .arrayLayers = 6,
                     .format = mapFormat(workload::prod::kIrradianceFormat),
                     .usage = TextureUsage::Sampled | TextureUsage::CopyDestination,
                     .label = label("lmx.noapi.bench.proto.irradiance")});
    descs.push_back({.kind = TextureKind::TextureCube,
                     .extent = {.width = workload::prod::kPrefilteredBaseFaceSize,
                                .height = workload::prod::kPrefilteredBaseFaceSize,
                                .depth = 1},
                     .mipCount = workload::prod::kPrefilteredMipCount,
                     .arrayLayers = 6,
                     .format = mapFormat(workload::prod::kPrefilteredFormat),
                     .usage = TextureUsage::Sampled | TextureUsage::CopyDestination,
                     .label = label("lmx.noapi.bench.proto.prefilteredEnv")});
    descs.push_back({.kind = TextureKind::Texture2D,
                     .extent = {.width = workload::prod::kDfgLutSize,
                                .height = workload::prod::kDfgLutSize,
                                .depth = 1},
                     .format = mapFormat(workload::prod::kDfgLutFormat),
                     .usage = TextureUsage::Sampled | TextureUsage::CopyDestination,
                     .label = label("lmx.noapi.bench.proto.dfgLut")});
    LMX_ASSERT(descs.size() == kTextureCount, "NoApiAdapter: texture descriptor count drifted");

    // Size the private allocation by simulating the same bump placement createTexture will be
    // given.
    uint64_t cursor = 0;
    uint64_t maxAlignment = 1;
    for (const TextureDesc& desc : descs) {
        const SizeAlign required = textureSizeAlign(m_device, desc);
        maxAlignment = std::max(maxAlignment, required.alignment);
        cursor = (cursor + required.alignment - 1) & ~(required.alignment - 1);
        cursor += required.size;
    }

    Result<Allocation> memory = allocate(m_device, {.size = cursor,
                                                    .alignment = maxAlignment,
                                                    .kind = MemoryKind::Private,
                                                    .label = "lmx.noapi.bench.proto.textures"});
    if (!memory) {
        std::cerr << "NoApiAdapter: " << memory.error().message << "\n";
        return false;
    }
    m_textureMemory = *memory;

    m_textures.reserve(kTextureCount);
    cursor = 0;
    for (const TextureDesc& desc : descs) {
        const SizeAlign required = textureSizeAlign(m_device, desc);
        cursor = (cursor + required.alignment - 1) & ~(required.alignment - 1);
        Result<Texture*> texture = createTexture(m_device, desc, m_textureMemory.gpu + cursor);
        if (!texture) {
            std::cerr << "NoApiAdapter: " << texture.error().message << "\n";
            return false;
        }
        cursor += required.size;
        m_textures.push_back(*texture);
    }
    return true;
}

//======================================================================================================================
bool NoApiAdapter::createSamplersAndTable() {
    // Mirrors RhiAdapter.cpp's three samplers exactly, which in turn mirror what
    // Source/Render/Renderer.cpp creates for the production scene pass.
    Result<Sampler*> linear =
        createSampler(m_device, {.maxAnisotropy = 16, .label = "lmx.noapi.bench.proto.linear"});
    Result<Sampler*> shadow = createSampler(m_device, {.addressU = AddressMode::ClampToEdge,
                                                       .addressV = AddressMode::ClampToEdge,
                                                       .addressW = AddressMode::ClampToEdge,
                                                       .maxAnisotropy = 16,
                                                       .compare = true,
                                                       .compareOp = CompareOp::GreaterEqual,
                                                       .label = "lmx.noapi.bench.proto.shadow"});
    Result<Sampler*> ibl = createSampler(m_device, {.addressU = AddressMode::ClampToEdge,
                                                    .addressV = AddressMode::ClampToEdge,
                                                    .addressW = AddressMode::ClampToEdge,
                                                    .label = "lmx.noapi.bench.proto.ibl"});
    if (!linear || !shadow || !ibl) {
        std::cerr << "NoApiAdapter: failed to create a sampler\n";
        return false;
    }
    m_linearSampler = *linear;
    m_shadowSampler = *shadow;
    m_iblSampler = *ibl;

    Result<DepthStencilState*> depthState =
        createDepthStencilState(m_device, {.depthTestEnabled = true,
                                           .depthWriteEnabled = true,
                                           .depthTest = CompareOp::Greater,
                                           .label = "lmx.noapi.bench.proto.reversedZ"});
    if (!depthState) {
        std::cerr << "NoApiAdapter: " << depthState.error().message << "\n";
        return false;
    }
    m_depthState = *depthState;

    Result<BindlessTable*> table = createBindlessTable(
        m_device, {.slotCount = kBindlessSlotCount, .label = "lmx.noapi.bench.proto.table"});
    if (!table) {
        std::cerr << "NoApiAdapter: " << table.error().message << "\n";
        return false;
    }
    m_table = *table;

    for (uint32_t material = 0; material < workload::kMaterialCount; ++material) {
        for (uint32_t role = 0; role < kMaterialSlotCount; ++role) {
            writeTextureSlot(m_table, kSlotMaterialBase + material * kMaterialSlotCount + role,
                             m_textures[kTexMaterialBase + material * kMaterialSlotCount + role],
                             {});
        }
    }
    writeTextureSlot(m_table, kSlotShadow, m_textures[kTexShadow], {});
    writeTextureSlot(m_table, kSlotIrradiance, m_textures[kTexIrradiance], {});
    writeTextureSlot(m_table, kSlotPrefiltered, m_textures[kTexPrefiltered], {});
    writeTextureSlot(m_table, kSlotDfgLut, m_textures[kTexDfgLut], {});
    writeSamplerSlot(m_table, kSlotLinearSampler, m_linearSampler);
    writeSamplerSlot(m_table, kSlotShadowSampler, m_shadowSampler);
    writeSamplerSlot(m_table, kSlotIblSampler, m_iblSampler);
    writeTextureSlot(m_table, kSlotSceneColor, m_textures[kTexSceneColor], {});
    for (uint32_t mip = 0; mip < workload::kBloomMipCount; ++mip) {
        const TextureViewDesc view{.baseMipLevel = mip, .mipCount = 1, .storage = true};
        writeTextureSlot(m_table, kSlotBloomAMip + mip, m_textures[kTexBloomA], view);
        writeTextureSlot(m_table, kSlotBloomBMip + mip, m_textures[kTexBloomB], view);
    }
    // The composite reads R7 whole and loads its mip 0, exactly as the incumbent binds the whole
    // texture there.
    writeTextureSlot(m_table, kSlotBloomBSampled, m_textures[kTexBloomB], {});
    return true;
}

//======================================================================================================================
bool NoApiAdapter::createPipelines() {
    if (!readShader("ProtoShadow", m_shadowShader) || !readShader("ProtoScene", m_sceneShader) ||
        !readShader("ProtoHistAccumulate", m_histAccumulateShader) ||
        !readShader("ProtoHistResolve", m_histResolveShader) ||
        !readShader("ProtoBloomThreshold", m_bloomThresholdShader) ||
        !readShader("ProtoBloomDown", m_bloomDownShader) ||
        !readShader("ProtoBloomUp", m_bloomUpShader) ||
        !readShader("ProtoComposite", m_compositeShader)) {
        return false;
    }

    // M5.1 Stage 4 (spec section 8's pipeline dimension, descriptive only): unlike the incumbent,
    // this prototype's pipelines take raw shader IR directly rather than a pre-loaded library
    // object, so createGraphicsPipeline/createComputePipeline is where the real compile cost lives
    // (readShader() above is comparatively cheap file I/O) -- that is what is timed here, once per
    // shader, in this adapter's one-time setup(). "Cold" holds by construction: createPipelines()
    // runs exactly once per process. metallib-vs-runtime-MSL is read off the same file-existence
    // check readShader() itself uses.
    const auto recordCompile = [&](std::string_view name,
                                   std::chrono::steady_clock::time_point start,
                                   std::chrono::steady_clock::time_point end) {
        std::error_code errorCode;
        const bool loadedMetallib = std::filesystem::exists(
            std::filesystem::path("Shaders") / (std::string(name) + ".metallib"), errorCode);
        m_pipelineCompileTimes.push_back(
            {.label = std::string(name),
             .coldNs = static_cast<uint64_t>(
                 std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()),
             .loadedMetallib = loadedMetallib});
    };
    const auto graphics = [&](std::string_view name, const std::vector<std::byte>& code,
                              std::span<const ColorTargetDesc> colorTargets, Format depthFormat,
                              std::string_view label, Pipeline*& out) {
        const auto start = std::chrono::steady_clock::now();
        Result<Pipeline*> pipeline = createGraphicsPipeline(
            m_device, {.vertex = {.ir = asCode(code), .entryPoint = "vertexMain"},
                       .pixel = {.ir = asCode(code), .entryPoint = "fragmentMain"},
                       .raster = {.topology = Topology::TriangleList,
                                  .sampleCount = 1,
                                  .depthFormat = depthFormat,
                                  .colorTargets = colorTargets},
                       .label = label});
        const auto end = std::chrono::steady_clock::now();
        if (!pipeline) {
            std::cerr << "NoApiAdapter: " << pipeline.error().message << "\n";
            return false;
        }
        out = *pipeline;
        recordCompile(name, start, end);
        return true;
    };
    const auto compute = [&](std::string_view name, const std::vector<std::byte>& code,
                             std::string_view entry, std::string_view label, Pipeline*& out) {
        const auto start = std::chrono::steady_clock::now();
        Result<Pipeline*> pipeline = createComputePipeline(
            m_device, {.compute = {.ir = asCode(code), .entryPoint = entry}, .label = label});
        const auto end = std::chrono::steady_clock::now();
        if (!pipeline) {
            std::cerr << "NoApiAdapter: " << pipeline.error().message << "\n";
            return false;
        }
        out = *pipeline;
        recordCompile(name, start, end);
        return true;
    };

    const std::array<ColorTargetDesc, 1> sceneTargets{
        ColorTargetDesc{.format = Format::RGBA16Float}};
    const std::array<ColorTargetDesc, 1> outTargets{ColorTargetDesc{.format = Format::RGBA8Unorm}};
    return graphics("ProtoShadow", m_shadowShader, {}, Format::D32Float,
                    "lmx.noapi.bench.proto.shadowPipeline", m_shadowPipeline) &&
           graphics("ProtoScene", m_sceneShader, sceneTargets, Format::D32Float,
                    "lmx.noapi.bench.proto.scenePipeline", m_scenePipeline) &&
           graphics("ProtoComposite", m_compositeShader, outTargets, Format::Undefined,
                    "lmx.noapi.bench.proto.compositePipeline", m_compositePipeline) &&
           compute("ProtoHistAccumulate", m_histAccumulateShader, "computeHistAccumulate",
                   "lmx.noapi.bench.proto.histAccumulatePipeline", m_histAccumulatePipeline) &&
           compute("ProtoHistResolve", m_histResolveShader, "computeHistResolve",
                   "lmx.noapi.bench.proto.histResolvePipeline", m_histResolvePipeline) &&
           compute("ProtoBloomThreshold", m_bloomThresholdShader, "computeBloomThreshold",
                   "lmx.noapi.bench.proto.bloomThresholdPipeline", m_bloomThresholdPipeline) &&
           compute("ProtoBloomDown", m_bloomDownShader, "computeBloomDown",
                   "lmx.noapi.bench.proto.bloomDownPipeline", m_bloomDownPipeline) &&
           compute("ProtoBloomUp", m_bloomUpShader, "computeBloomUp",
                   "lmx.noapi.bench.proto.bloomUpPipeline", m_bloomUpPipeline);
}

//======================================================================================================================
bool NoApiAdapter::uploadStaticAssets() {
    CommandBuffer* commands =
        beginCommands(m_queue, &m_stagingAllocator, "lmx.noapi.bench.proto.upload");

    const auto stage = [&](const void* data, uint64_t size,
                           uint64_t alignment = kStagingAlignment) {
        const Suballocation storage = m_stagingAllocator.allocate(size, alignment);
        std::memcpy(storage.cpu, data, size);
        return storage.gpu;
    };

    for (uint32_t material = 0; material < workload::kMaterialCount; ++material) {
        for (uint32_t role = 0; role < kMaterialSlotCount; ++role) {
            const workload::GeneratedTexture generated =
                workload::generateMaterialTexture(material, kMaterialRoles[role]);
            Texture* texture = m_textures[kTexMaterialBase + material * kMaterialSlotCount + role];
            for (uint32_t level = 0; level < generated.mips.size(); ++level) {
                const workload::MipLevel& mip = generated.mips[level];
                const GpuAddress source = stage(mip.rgba8.data(), mip.rgba8.size());
                copyToTexture(commands, texture,
                              {.mipLevel = level,
                               .arrayLayer = 0,
                               .origin = {},
                               .extent = {.width = mip.width, .height = mip.height, .depth = 1}},
                              source, {.bytesPerRow = uint64_t{mip.width} * 4, .bytesPerImage = 0});
            }
        }
    }

    const auto uploadCube = [&](const workload::SyntheticCubemap& cube, Texture* texture) {
        for (uint32_t level = 0; level < cube.mips.size(); ++level) {
            const uint32_t faceSize = std::max(cube.faceSize >> level, 1u);
            for (uint32_t face = 0; face < 6; ++face) {
                const std::vector<uint16_t>& texels = cube.mips[level][face].texelsRgba16;
                const GpuAddress source = stage(texels.data(), texels.size() * sizeof(uint16_t));
                copyToTexture(commands, texture,
                              {.mipLevel = level,
                               .arrayLayer = face,
                               .origin = {},
                               .extent = {.width = faceSize, .height = faceSize, .depth = 1}},
                              source, {.bytesPerRow = uint64_t{faceSize} * 8, .bytesPerImage = 0});
            }
        }
    };
    uploadCube(workload::generateSyntheticIrradiance(), m_textures[kTexIrradiance]);
    uploadCube(workload::generateSyntheticPrefilteredEnv(), m_textures[kTexPrefiltered]);

    {
        const workload::DfgLut lut = workload::generateSyntheticDfgLut();
        const GpuAddress source =
            stage(lut.texelsRg16.data(), lut.texelsRg16.size() * sizeof(uint16_t) * 2);
        copyToTexture(commands, m_textures[kTexDfgLut],
                      {.mipLevel = 0,
                       .arrayLayer = 0,
                       .origin = {},
                       .extent = {.width = lut.size, .height = lut.size, .depth = 1}},
                      source, {.bytesPerRow = uint64_t{lut.size} * 4, .bytesPerImage = 0});
    }

    endCommands(commands);
    Result<Semaphore*> fence = createSemaphore(m_device, 0, "lmx.noapi.bench.proto.uploadFence");
    if (!fence) {
        std::cerr << "NoApiAdapter: " << fence.error().message << "\n";
        return false;
    }
    const std::array<CommandBuffer*, 1> list{commands};
    submit(m_queue, list, *fence, 1);
    waitSemaphore(*fence, 1);
    destroySemaphore(m_device, *fence);

    // Staging is setup-only: releasing it here keeps the resident-bytes dimension Stage 4 measures
    // to what a measured frame actually needs.
    m_stagingAllocator = LinearAllocator();
    deallocate(m_device, m_staging);
    m_staging = Allocation{};
    return true;
}

//======================================================================================================================
void NoApiAdapter::writePersistentRoots() {
    LinearAllocator roots(m_persistentRoots);
    const GpuAddress table = bindlessTableAddress(m_table);

    // P03's per-draw blocks. The shadow light and its fitted frustum are fixed for the whole run
    // and object transforms are static, so every `mvp` here is a constant of the workload.
    const lmx::render::ShadowMatrices shadow =
        lmx::render::fitShadowOrtho(glm::vec4(0.0f, 0.0f, 0.0f, kBoundingRadius), kLightDirection);
    const Suballocation shadowRoots = roots.allocate<ShadowObjectRoot>(workload::kDrawCount);
    m_shadowRootBase = shadowRoots.gpu;
    auto* shadowBlocks = static_cast<ShadowObjectRoot*>(shadowRoots.cpu);
    for (uint32_t draw = 0; draw < workload::kDrawCount; ++draw) {
        const workload::DrawPlacement placement = workload::drawPlacement(draw);
        const glm::mat4 model =
            glm::translate(glm::mat4(1.0f), glm::vec3(placement.x, 0.0f, placement.z));
        shadowBlocks[draw] = ShadowObjectRoot{
            .mvp = shadow.viewProj * model, .vertices = m_quadVertexAddress, .padding = {0, 0}};
    }

    const auto write = [&](const auto& block) {
        using Block = std::remove_cvref_t<decltype(block)>;
        const Suballocation storage = roots.allocate(sizeof(Block), alignof(Block));
        std::memcpy(storage.cpu, &block, sizeof(Block));
        return storage.gpu;
    };

    m_histAccumulateRoot = write(HistAccumulateRoot{
        .slots = glm::uvec4(kSlotSceneColor, 0, 0, 0),
        .range = glm::vec4(kExposureLogLuminanceMin, kExposureLogLuminanceMax, 0.0f, 0.0f),
        .textures = table,
        .exposure = m_exposureAddress,
        .histogram = m_histogramAddress,
        .padding = {0, 0}});
    m_histResolveRoot = write(HistResolveRoot{
        .percentiles = glm::vec4(50.0f, 95.0f, 0.18f, 0.0f),
        .evRange = glm::vec4(-8.0f, 8.0f, kExposureLogLuminanceMin, kExposureLogLuminanceMax),
        .histogram = m_histogramAddress,
        .exposure = m_exposureAddress});
    m_bloomThresholdRoot =
        write(BloomThresholdRoot{.slots = glm::uvec4(kSlotSceneColor, kSlotBloomAMip, 0, 0),
                                 .params = glm::vec4(kBloomThreshold, 0.0f, 0.0f, 0.0f),
                                 .textures = table,
                                 .images = table});

    // P08 and P09 differ only in their extents and slot pair, both frozen by the manifest.
    const auto downRoot = [&](uint32_t sourceSlot, uint32_t sourceExtent, uint32_t destinationSlot,
                              uint32_t destinationExtent) {
        return write(BloomDownRoot{
            .extents = glm::uvec4(sourceExtent, sourceExtent, destinationExtent, destinationExtent),
            .slots = glm::uvec4(sourceSlot, destinationSlot, 0, 0),
            .images = table,
            .padding = {0, 0}});
    };
    m_bloomDownRoot[0] = downRoot(kSlotBloomAMip + 0, workload::kBloomExtent, kSlotBloomAMip + 1,
                                  workload::kBloomExtent / 2);
    m_bloomDownRoot[1] = downRoot(kSlotBloomAMip + 1, workload::kBloomExtent / 2,
                                  kSlotBloomAMip + 2, workload::kBloomExtent / 4);

    const auto upRoot = [&](uint32_t baseSlot, uint32_t smallSlot, uint32_t smallExtent,
                            uint32_t destinationSlot, uint32_t destinationExtent) {
        return write(BloomUpRoot{
            .extents = glm::uvec4(smallExtent, smallExtent, destinationExtent, destinationExtent),
            .slots = glm::uvec4(baseSlot, smallSlot, destinationSlot, 0),
            .images = table,
            .padding = {0, 0}});
    };
    m_bloomUpRoot[0] = upRoot(kSlotBloomAMip + 1, kSlotBloomAMip + 2, workload::kBloomExtent / 4,
                              kSlotBloomBMip + 1, workload::kBloomExtent / 2);
    m_bloomUpRoot[1] = upRoot(kSlotBloomAMip + 0, kSlotBloomBMip + 1, workload::kBloomExtent / 2,
                              kSlotBloomBMip + 0, workload::kBloomExtent);

    m_compositeRoot =
        write(CompositeRoot{.slots = glm::uvec4(kSlotSceneColor, kSlotBloomBSampled, 0, 0),
                            .params = glm::vec4(kBloomIntensity, 0.0f, 0.0f, 0.0f),
                            .textures = table,
                            .exposure = m_exposureAddress});
}

//======================================================================================================================
void NoApiAdapter::planBarriers() {
    // Reduces the manifest's declared per-resource uses to one producer/consumer stage pair per
    // pass boundary. A resource-state walk, exactly like the production graph's, but the output is
    // stage pairs rather than per-subresource transitions, because that is the entire barrier
    // vocabulary this interface has (Types.h's Stage doc comment: "Stages, not resources, are the
    // whole barrier vocabulary"). A dependency is emitted only when the resource's state actually
    // changes, so a second reader in a stage already ordered against the writer adds nothing --
    // Metal's stage barriers are global, so the first one already ordered it.
    struct ResourceState {
        Stage lastWrite = Stage::None;
        Stage readsSinceWrite = Stage::None;
    };
    // Keyed per subresource, so the bloom chain's mip-to-mip dependencies are not collapsed into
    // one whole-resource state; a whole-resource use expands to every level the resource declares,
    // which is what makes P02's mip-0 write to material 0's emissive texture pair with P04's
    // whole-texture read of it.
    std::map<std::pair<std::string, uint32_t>, ResourceState> state;

    const workload::RepresentativeGraph& manifest = workload::representativeGraph();
    LMX_ASSERT(manifest.passes.size() == kPassCount, "NoApiAdapter: manifest pass count drifted");

    std::map<std::string, uint32_t> levelCount;
    for (const workload::TextureResource& resource : manifest.textures) {
        levelCount[resource.id] = resource.mipLevels;
    }
    for (const workload::BufferResource& resource : manifest.buffers) {
        levelCount[resource.id] = 1;
    }

    for (size_t index = 0; index < manifest.passes.size(); ++index) {
        const workload::PassDeclaration& pass = manifest.passes[index];
        Stage producers = Stage::None;
        Stage consumers = Stage::None;

        // Every raster read in this graph is a fragment-stage read (the shadow map, the emissive
        // texture); the vertex stage reads only addresses, which carry no barrier of their own.
        const Stage passStage = pass.kind == workload::PassKind::Copy      ? Stage::Copy
                                : pass.kind == workload::PassKind::Compute ? Stage::Compute
                                                                           : Stage::PixelShader;
        const auto writeStage = [&](const workload::ResourceUse& use) {
            switch (use.role) {
            case workload::UseRole::ColorAttachment:
                return Stage::RasterColorOut;
            case workload::UseRole::DepthAttachment:
                return Stage::RasterDepthOut;
            default:
                break;
            }
            return pass.kind == workload::PassKind::Copy ? Stage::Copy : Stage::Compute;
        };

        for (const workload::ResourceUse& use : pass.uses) {
            const bool writes = use.role == workload::UseRole::Write ||
                                use.role == workload::UseRole::ColorAttachment ||
                                use.role == workload::UseRole::DepthAttachment ||
                                use.role == workload::UseRole::CopyDestination;
            const Stage stage = writes ? writeStage(use) : passStage;
            const uint32_t levels = levelCount.at(use.resourceId);
            const uint32_t first = use.range.baseMipLevel;
            const uint32_t last =
                use.range.mipLevelCount == 0 ? levels : first + use.range.mipLevelCount;

            for (uint32_t level = first; level < last; ++level) {
                ResourceState& entry = state[{use.resourceId, level}];
                if (!writes) {
                    if (entry.lastWrite != Stage::None && !hasStage(entry.readsSinceWrite, stage)) {
                        producers = producers | entry.lastWrite;
                        consumers = consumers | stage;
                    }
                    entry.readsSinceWrite = entry.readsSinceWrite | stage;
                    continue;
                }
                if (entry.lastWrite != Stage::None) {
                    producers = producers | entry.lastWrite;
                    consumers = consumers | stage;
                }
                if (entry.readsSinceWrite != Stage::None) {
                    producers = producers | entry.readsSinceWrite;
                    consumers = consumers | stage;
                }
                entry.lastWrite = stage;
                entry.readsSinceWrite = Stage::None;
            }
        }

        m_barrierBeforePass[index] = {.producer = producers,
                                      .consumer = consumers,
                                      .hazards = Hazard::None,
                                      .present =
                                          producers != Stage::None && consumers != Stage::None};
    }
}

//======================================================================================================================
void NoApiAdapter::emitBarrier(CommandBuffer* commands, size_t passIndex) {
    const StageBarrier& planned = m_barrierBeforePass[passIndex];
    if (!planned.present) {
        return;
    }
    barrier(commands, planned.producer, planned.consumer, planned.hazards);
}

//======================================================================================================================
void NoApiAdapter::writeEmissiveStaging(uint32_t frameIndex) {
    const uint32_t slot = frameIndex % workload::kEmissiveRingSlotCount;
    auto* bytes = static_cast<uint8_t*>(m_emissiveRing.cpu) +
                  uint64_t{slot} * workload::kEmissiveRingSlotSize;
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
}

//======================================================================================================================
void NoApiAdapter::encodeFillHistogram(CommandBuffer* commands) {
    fillMemory(commands, m_histogramAddress, workload::kHistogramBufferSize, 0);
}

//======================================================================================================================
void NoApiAdapter::encodeUploadEmissive(CommandBuffer* commands, uint32_t frameIndex) {
    const uint32_t slot = frameIndex % workload::kEmissiveRingSlotCount;
    copyToTexture(
        commands, m_textures[kTexMaterialZeroEmissive],
        {.mipLevel = 0,
         .arrayLayer = 0,
         .origin = {},
         .extent = {.width = workload::kMaterialTextureSize,
                    .height = workload::kMaterialTextureSize,
                    .depth = 1}},
        m_emissiveRing.gpu + uint64_t{slot} * workload::kEmissiveRingSlotSize,
        {.bytesPerRow = uint64_t{workload::kMaterialTextureSize} * 4, .bytesPerImage = 0});
}

//======================================================================================================================
void NoApiAdapter::encodeShadow(CommandBuffer* commands) {
    const DepthAttachment depth{.texture = m_textures[kTexShadow],
                                .load = LoadAction::Clear,
                                .store = StoreAction::Store,
                                .clearDepth = 0.0f};
    beginRenderPass(commands, {.depth = &depth, .label = "lmx.noapi.bench.proto.shadow"});
    setPipeline(commands, m_shadowPipeline);
    setDepthStencilState(commands, m_depthState);
    // Stated rather than inherited from the attachment, so this pass's rasterizer configuration is
    // the same set of explicit values the incumbent's backend replays on every pipeline bind.
    setViewport(commands, {.width = static_cast<float>(workload::kShadowExtent),
                           .height = static_cast<float>(workload::kShadowExtent)});
    setCullMode(commands, CullMode::Back);
    setFrontFace(commands, Winding::CounterClockwise);
    setDepthBias(commands, kShadowDepthBiasConstant, kShadowDepthBiasSlopeScale, 0.0f);

    // Every block this pass reads was written once in setup() and is addressed here by pointer
    // arithmetic; the fragment stage reads no root data, so each draw costs exactly one
    // `setAddress` and no CPU-side data movement at all.
    for (uint32_t draw = 0; draw < workload::kDrawCount; ++draw) {
        const GpuAddress root = m_shadowRootBase + uint64_t{draw} * sizeof(ShadowObjectRoot);
        drawIndexed(commands, root, kNullAddress, m_quadIndexAddress, IndexKind::Uint32, 6);
    }
    endRenderPass(commands);
}

//======================================================================================================================
void NoApiAdapter::encodeScene(CommandBuffer* commands, const glm::mat4& viewProj,
                               const glm::mat4& shadowTransform) {
    const ColorAttachment color{.texture = m_textures[kTexSceneColor],
                                .load = LoadAction::Clear,
                                .store = StoreAction::Store,
                                .clearColor = {0.0f, 0.0f, 0.0f, 1.0f}};
    const DepthAttachment depth{.texture = m_textures[kTexSceneDepth],
                                .load = LoadAction::Clear,
                                .store = StoreAction::DontCare,
                                .clearDepth = 0.0f};
    const std::array<ColorAttachment, 1> colorTargets{color};
    beginRenderPass(
        commands,
        {.colorTargets = colorTargets, .depth = &depth, .label = "lmx.noapi.bench.proto.scene"});
    setPipeline(commands, m_scenePipeline);
    setDepthStencilState(commands, m_depthState);
    setViewport(commands, {.width = static_cast<float>(workload::kSceneWidth),
                           .height = static_cast<float>(workload::kSceneHeight)});
    setCullMode(commands, CullMode::Back);
    setFrontFace(commands, Winding::CounterClockwise);
    setDepthBias(commands, 0.0f, 0.0f, 0.0f);

    const GpuAddress table = bindlessTableAddress(m_table);
    ScenePassRoot pass{};
    pass.viewProj = viewProj;
    pass.shadowTransform = shadowTransform;
    pass.eyePositionAndTime = glm::vec4(0.0f);
    pass.preExposure = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    pass.lightStrength0 = glm::vec4(3.0f, 3.0f, 3.0f, 0.0f);
    pass.lightDirection0 = glm::vec4(kLightDirection, 0.0f);
    pass.lightStrength1 = glm::vec4(0.0f);
    pass.lightDirection1 = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);
    pass.lightStrength2 = glm::vec4(0.0f);
    pass.lightDirection2 = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);
    pass.sharedSlots = glm::uvec4(kSlotShadow, kSlotIrradiance, kSlotPrefiltered, kSlotDfgLut);
    pass.samplerSlots =
        glm::uvec4(kSlotLinearSampler, kSlotShadowSampler, kSlotIblSampler, kShadowFilterPcf);
    pass.textures = table;
    pass.cubes = table;
    pass.samplers = table;
    pass.comparisonSamplers = table;
    const GpuAddress passAddress = pushRoot(commands, pass);

    for (uint32_t draw = 0; draw < workload::kDrawCount; ++draw) {
        const uint32_t material = workload::drawMaterialIndex(draw);
        const workload::DrawMaterialParams params = workload::drawMaterialParams(draw);
        const workload::DrawPlacement placement = workload::drawPlacement(draw);
        const glm::mat4 model =
            glm::translate(glm::mat4(1.0f), glm::vec3(placement.x, 0.0f, placement.z));
        const uint32_t materialSlot = kSlotMaterialBase + material * kMaterialSlotCount;

        SceneObjectRoot root{};
        root.mvp = viewProj * model;
        root.model = model;
        // No rotation or non-uniform scale in this placement.
        root.normalMatrix = glm::mat4(1.0f);
        root.uvTransform = glm::mat4(1.0f);
        root.albedo = glm::vec4(1.0f);
        root.materialParams = glm::vec4(params.roughness, params.metallic, 1.0f, 0.0f);
        root.emissive = glm::vec4(glm::vec3(params.emissiveScale), 0.0f);
        root.flagsAndSlots =
            glm::uvec4(kFlagHasNormalMap, materialSlot + 0, materialSlot + 1, materialSlot + 2);
        root.materialSlots = glm::uvec4(materialSlot + 3, materialSlot + 4, 0, 0);
        root.vertices = m_quadVertexAddress;
        root.padding[0] = 0;
        root.padding[1] = 0;

        // Two root addresses per draw and zero descriptors (evidence section 6.1): the per-draw
        // block at the first root index and the frame's pass block, unchanged for all 1,024 draws,
        // re-bound at the second. Nothing here deduplicates the repeat.
        const GpuAddress address = pushRoot(commands, root);
        drawIndexed(commands, address, passAddress, m_quadIndexAddress, IndexKind::Uint32, 6);
    }
    endRenderPass(commands);
}

//======================================================================================================================
void NoApiAdapter::encodeHistogramAccumulate(CommandBuffer* commands) {
    setPipeline(commands, m_histAccumulatePipeline);
    dispatch(commands, m_histAccumulateRoot, workload::kSceneWidth / 8, workload::kSceneHeight / 8,
             1);
}

//======================================================================================================================
void NoApiAdapter::encodeHistogramResolve(CommandBuffer* commands) {
    setPipeline(commands, m_histResolvePipeline);
    dispatch(commands, m_histResolveRoot, 1, 1, 1);
}

//======================================================================================================================
void NoApiAdapter::encodeBloomThreshold(CommandBuffer* commands) {
    setPipeline(commands, m_bloomThresholdPipeline);
    dispatch(commands, m_bloomThresholdRoot, workload::kBloomExtent / 8, workload::kBloomExtent / 8,
             1);
}

//======================================================================================================================
void NoApiAdapter::encodeBloomDown(CommandBuffer* commands, GpuAddress root,
                                   uint32_t destinationExtent) {
    setPipeline(commands, m_bloomDownPipeline);
    dispatch(commands, root, (destinationExtent + 7) / 8, (destinationExtent + 7) / 8, 1);
}

//======================================================================================================================
void NoApiAdapter::encodeBloomUp(CommandBuffer* commands, GpuAddress root,
                                 uint32_t destinationExtent) {
    setPipeline(commands, m_bloomUpPipeline);
    dispatch(commands, root, (destinationExtent + 7) / 8, (destinationExtent + 7) / 8, 1);
}

//======================================================================================================================
void NoApiAdapter::encodeComposite(CommandBuffer* commands) {
    const ColorAttachment color{.texture = m_textures[kTexOut],
                                .load = LoadAction::Clear,
                                .store = StoreAction::Store,
                                .clearColor = {0.0f, 0.0f, 0.0f, 1.0f}};
    const std::array<ColorAttachment, 1> colorTargets{color};
    beginRenderPass(commands,
                    {.colorTargets = colorTargets, .label = "lmx.noapi.bench.proto.composite"});
    setPipeline(commands, m_compositePipeline);
    setViewport(commands, {.width = static_cast<float>(workload::kSceneWidth),
                           .height = static_cast<float>(workload::kSceneHeight)});
    setCullMode(commands, CullMode::None);
    setFrontFace(commands, Winding::CounterClockwise);
    setDepthBias(commands, 0.0f, 0.0f, 0.0f);

    // The vertex stage reads no root data at all, so only the pixel root's address is bound.
    draw(commands, kNullAddress, m_compositeRoot, 3);
    endRenderPass(commands);
}

//======================================================================================================================
void NoApiAdapter::encodeReadback(CommandBuffer* commands) {
    copyFromTexture(
        commands, m_readback.gpu,
        {.bytesPerRow = uint64_t{workload::kSceneWidth} * 4, .bytesPerImage = 0},
        m_textures[kTexOut],
        {.mipLevel = 0,
         .arrayLayer = 0,
         .origin = {},
         .extent = {.width = workload::kSceneWidth, .height = workload::kSceneHeight, .depth = 1}});
    if (m_diagScene) {
        // Unscored diagnostic: the P13 barrier already ordered the scene pass's fragment writes
        // before this copy, so R2 is readable here without a barrier of its own.
        copyFromTexture(commands, m_diagSceneColor.gpu,
                        {.bytesPerRow = uint64_t{workload::kSceneWidth} * 8, .bytesPerImage = 0},
                        m_textures[kTexSceneColor],
                        {.mipLevel = 0,
                         .arrayLayer = 0,
                         .origin = {},
                         .extent = {.width = workload::kSceneWidth,
                                    .height = workload::kSceneHeight,
                                    .depth = 1}});
    }
}

//======================================================================================================================
void NoApiAdapter::runFrame(uint32_t frameIndex, std::vector<uint8_t>& outReadback) {
    const uint32_t slot = m_ring.beginFrame();

    // ---- BEGIN TIMED REGION -----------------------------------------------------------------
    // M5.1 Stage 4 (spec section 8): std::chrono::steady_clock on both adapters, wrapping exactly
    // the span this file's header comment already documents as the timed region. The frame-slot
    // pacing wait above (m_ring.beginFrame()) is excluded by construction -- the clock starts after
    // it returns.
    const auto timedRegionStart = std::chrono::steady_clock::now();
    writeEmissiveStaging(frameIndex);

    const workload::CameraPose camera = workload::cameraForFrame(frameIndex);
    const glm::mat4 view = glm::lookAt(glm::vec3(camera.eyeX, camera.eyeY, camera.eyeZ),
                                       glm::vec3(camera.targetX, camera.targetY, camera.targetZ),
                                       glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 proj = reversedInfiniteFarProjection(glm::radians(60.0f), 1.0f, 0.1f);
    const glm::mat4 viewProj = proj * view;
    const lmx::render::ShadowMatrices shadow =
        lmx::render::fitShadowOrtho(glm::vec4(0.0f, 0.0f, 0.0f, kBoundingRadius), kLightDirection);

    CommandBuffer* commands =
        beginCommands(m_queue, &m_ring.rootAllocator(), "lmx.noapi.bench.proto.frame");
    setBindlessTable(commands, m_table);

    emitBarrier(commands, 0);
    encodeFillHistogram(commands); // P01
    emitBarrier(commands, 1);
    encodeUploadEmissive(commands, frameIndex); // P02
    emitBarrier(commands, 2);
    encodeShadow(commands); // P03
    emitBarrier(commands, 3);
    encodeScene(commands, viewProj, shadow.shadowTransform); // P04
    emitBarrier(commands, 4);
    encodeHistogramAccumulate(commands); // P05
    emitBarrier(commands, 5);
    encodeHistogramResolve(commands); // P06
    emitBarrier(commands, 6);
    encodeBloomThreshold(commands); // P07
    emitBarrier(commands, 7);
    encodeBloomDown(commands, m_bloomDownRoot[0], workload::kBloomExtent / 2); // P08
    emitBarrier(commands, 8);
    encodeBloomDown(commands, m_bloomDownRoot[1], workload::kBloomExtent / 4); // P09
    emitBarrier(commands, 9);
    encodeBloomUp(commands, m_bloomUpRoot[0], workload::kBloomExtent / 2); // P10
    emitBarrier(commands, 10);
    encodeBloomUp(commands, m_bloomUpRoot[1], workload::kBloomExtent); // P11
    emitBarrier(commands, 11);
    encodeComposite(commands); // P12
    emitBarrier(commands, 12);
    encodeReadback(commands); // P13

    endCommands(commands);
    const CommandBufferStats frameStats = commandBufferStats(commands);
    const std::array<CommandBuffer*, 1> list{commands};
    m_ring.endFrame(m_queue, list);
    const auto timedRegionEnd = std::chrono::steady_clock::now();
    // ---- END TIMED REGION -------------------------------------------------------------------

    m_lastFrameTimedRegionNs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(timedRegionEnd - timedRegionStart)
            .count());
    const BindlessTableStats tableStats = bindlessTableStats(m_table);
    m_lastFrameCounters = FrameBindingCounters{
        .setAddressCalls = frameStats.setAddressCalls,
        .pushRootCalls = frameStats.rootCalls,
        .pushRootBytes = frameStats.rootBytes,
        .tableWriteCalls = tableStats.writeCalls - m_tableWriteCallsAtLastSample,
        .tableWriteBytes = tableStats.writeBytes - m_tableWriteBytesAtLastSample,
        .barrierCalls = frameStats.barrierCalls,
    };
    m_tableWriteCallsAtLastSample = tableStats.writeCalls;
    m_tableWriteBytesAtLastSample = tableStats.writeBytes;

    while (!m_ring.isSlotRetired(slot)) {
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
    if (m_diagScene) {
        outReadback.resize(m_diagSceneColor.size);
        std::memcpy(outReadback.data(), m_diagSceneColor.cpu, outReadback.size());
        return;
    }
    outReadback.resize(workload::kReadbackBufferSize);
    std::memcpy(outReadback.data(), m_readback.cpu, outReadback.size());
}

//======================================================================================================================
AllocationSnapshot NoApiAdapter::allocationSnapshot() const {
    if (m_device == nullptr) {
        return {};
    }
    const DeviceCreationStats creation = deviceCreationStats(m_device);
    AllocationSnapshot snapshot{.textureCreateCalls = creation.liveTextures,
                                .bufferCreateCalls = creation.liveAllocations,
                                .samplerCreateCalls = creation.liveSamplers,
                                .pipelineCreateCalls = creation.livePipelines,
                                .residentBytesIsMetalReported = true};
    if (m_residency != nullptr) {
        snapshot.residentBytes = residentBytes(m_residency);
    }
    return snapshot;
}

//======================================================================================================================
void NoApiAdapter::teardown() {
    if (m_device == nullptr || m_torndown) {
        return;
    }
    m_torndown = true;

    // The ring waits for every frame it paced before releasing its slots, which is also the proof
    // every destruction contract below needs.
    m_ring = FrameRing();

    if (m_table != nullptr) {
        for (uint32_t slot = 0; slot < kBindlessSlotCount; ++slot) {
            clearBindlessSlot(m_table, slot);
        }
    }
    for (Pipeline* pipeline : {m_shadowPipeline, m_scenePipeline, m_compositePipeline,
                               m_histAccumulatePipeline, m_histResolvePipeline,
                               m_bloomThresholdPipeline, m_bloomDownPipeline, m_bloomUpPipeline}) {
        if (pipeline != nullptr) {
            destroyPipeline(m_device, pipeline);
        }
    }
    if (m_depthState != nullptr) {
        destroyDepthStencilState(m_device, m_depthState);
    }
    for (Sampler* sampler : {m_linearSampler, m_shadowSampler, m_iblSampler}) {
        if (sampler != nullptr) {
            destroySampler(m_device, sampler);
        }
    }
    for (Texture* texture : m_textures) {
        destroyTexture(m_device, texture);
    }
    m_textures.clear();
    if (m_table != nullptr) {
        destroyBindlessTable(m_device, m_table);
        m_table = nullptr;
    }
    for (Allocation* allocation :
         {&m_diagSceneColor, &m_readback, &m_persistentRoots, &m_emissiveRing, &m_storageBuffers,
          &m_geometry, &m_staging, &m_textureMemory}) {
        if (allocation->size > 0) {
            deallocate(m_device, *allocation);
            *allocation = Allocation{};
        }
    }
    if (m_residency != nullptr) {
        destroyResidencySet(m_device, m_residency);
        m_residency = nullptr;
    }
    destroyDevice(m_device);
    m_device = nullptr;
}

} // namespace lmx::noapi::bench
