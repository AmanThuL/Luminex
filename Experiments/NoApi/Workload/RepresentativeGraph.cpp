//----------------------------------------------------------------------------------------------------------------------
/// @file RepresentativeGraph.cpp
/// @brief Implements RepresentativeGraph for the NoApi experiment.
//----------------------------------------------------------------------------------------------------------------------

/// @details Defines the frozen thirteen-pass representative graph's resource table, pass table, and
///        sinks (spec section 6).

#include "Workload/RepresentativeGraph.h"
#include "Workload/ProdValues.h"

namespace lmx::noapi::workload {

namespace {

// Whole-resource use: the manifest's stand-in for rhi::kAllMipLevels (SubresourceRange's
// mipLevelCount == 0 means "the whole chain").
constexpr SubresourceRange kWholeResource{};

//======================================================================================================================
SubresourceRange mip(uint32_t level) {
    return SubresourceRange{.baseMipLevel = level, .mipLevelCount = 1};
}

//======================================================================================================================
std::vector<TextureResource> buildTextures() {
    return {
        {.id = "R1",
         .name = "shadow",
         .width = kShadowExtent,
         .height = kShadowExtent,
         .format = Format::D32Float,
         .mipLevels = 1,
         .depthTarget = true,
         .sampled = true},
        {.id = "R2",
         .name = "sceneColor",
         .width = kSceneWidth,
         .height = kSceneHeight,
         .format = Format::RGBA16Float,
         .mipLevels = 1,
         .renderTarget = true,
         .sampled = true},
        {.id = "R3",
         .name = "sceneDepth",
         .width = kSceneWidth,
         .height = kSceneHeight,
         .format = Format::D32Float,
         .mipLevels = 1,
         .depthTarget = true},
        {.id = "R6",
         .name = "bloomA",
         .width = kBloomExtent,
         .height = kBloomExtent,
         .format = Format::RGBA16Float,
         .mipLevels = kBloomMipCount,
         .sampled = true,
         .storageRead = true,
         .storageWrite = true},
        {.id = "R7",
         .name = "bloomB",
         .width = kBloomExtent,
         .height = kBloomExtent,
         .format = Format::RGBA16Float,
         .mipLevels = kBloomMipCount,
         .sampled = true,
         .storageRead = true,
         .storageWrite = true},
        {.id = "R8",
         .name = "out",
         .width = kSceneWidth,
         .height = kSceneHeight,
         .format = Format::RGBA8Unorm,
         .mipLevels = 1,
         .renderTarget = true,
         .copySource = true},
        // Not one of R1-R10: see RepresentativeGraph.h's kMaterialZeroEmissiveId doc comment for
        // why this one Materials texture needs its own tracked graph resource.
        {.id = kMaterialZeroEmissiveId,
         .name = "material0.emissive",
         .width = kMaterialTextureSize,
         .height = kMaterialTextureSize,
         .format = prod::kEmissiveFormat,
         .mipLevels = 7, // 64, 32, 16, 8, 4, 2, 1
         .sampled = true,
         .copyDestination = true},
    };
}

//======================================================================================================================
std::vector<BufferResource> buildBuffers() {
    return {
        {.id = "R4",
         .name = "histogram",
         .size = kHistogramBufferSize,
         .storageRead = true,
         .storageWrite = true,
         .copyDestination = true},
        {.id = "R5",
         .name = "exposure",
         .size = prod::kExposureBufferSize,
         .storageRead = prod::kExposureBufferStorageRead,
         .storageWrite = prod::kExposureBufferStorageWrite,
         .persistent = true},
        {.id = "R9",
         .name = "readback",
         .size = kReadbackBufferSize,
         .copyDestination = true,
         .cpuReadback = true},
        {.id = "R10",
         .name = "emissiveStaging",
         .size = kEmissiveRingSlotSize * kEmissiveRingSlotCount,
         .copySource = true},
    };
}

//======================================================================================================================
std::vector<PassDeclaration> buildPasses() {
    return {
        {.id = "P01",
         .label = "fill.histogram",
         .kind = PassKind::Copy,
         .uses = {{.resourceId = "R4", .role = UseRole::CopyDestination}}},
        {.id = "P02",
         .label = "upload.emissive",
         .kind = PassKind::Copy,
         .uses = {{.resourceId = "R10", .role = UseRole::CopySource},
                  {.resourceId = kMaterialZeroEmissiveId,
                   .role = UseRole::CopyDestination,
                   .range = mip(0)}}},
        {.id = "P03",
         .label = "shadow",
         .kind = PassKind::Raster,
         .uses = {{.resourceId = "R1", .role = UseRole::DepthAttachment}}},
        {.id = "P04",
         .label = "scene",
         .kind = PassKind::Raster,
         .uses = {{.resourceId = "R1", .role = UseRole::Read, .range = kWholeResource},
                  {.resourceId = kMaterialZeroEmissiveId, .role = UseRole::Read},
                  {.resourceId = "R2", .role = UseRole::ColorAttachment},
                  {.resourceId = "R3", .role = UseRole::DepthAttachment}}},
        {.id = "P05",
         .label = "histogram.accumulate",
         .kind = PassKind::Compute,
         .uses = {{.resourceId = "R2", .role = UseRole::ShaderRead},
                  {.resourceId = "R5", .role = UseRole::ShaderRead},
                  {.resourceId = "R4", .role = UseRole::Write}}},
        {.id = "P06",
         .label = "histogram.resolve",
         .kind = PassKind::Compute,
         .uses = {{.resourceId = "R4", .role = UseRole::Read},
                  {.resourceId = "R5", .role = UseRole::Write}}},
        {.id = "P07",
         .label = "bloom.threshold",
         .kind = PassKind::Compute,
         .uses = {{.resourceId = "R2", .role = UseRole::ShaderRead},
                  {.resourceId = "R6", .role = UseRole::Write, .range = mip(0)}}},
        {.id = "P08",
         .label = "bloom.down1",
         .kind = PassKind::Compute,
         .uses = {{.resourceId = "R6", .role = UseRole::Read, .range = mip(0)},
                  {.resourceId = "R6", .role = UseRole::Write, .range = mip(1)}}},
        {.id = "P09",
         .label = "bloom.down2",
         .kind = PassKind::Compute,
         .uses = {{.resourceId = "R6", .role = UseRole::Read, .range = mip(1)},
                  {.resourceId = "R6", .role = UseRole::Write, .range = mip(2)}}},
        {.id = "P10",
         .label = "bloom.up1",
         .kind = PassKind::Compute,
         .uses = {{.resourceId = "R6", .role = UseRole::Read, .range = mip(1)},
                  {.resourceId = "R6", .role = UseRole::Read, .range = mip(2)},
                  {.resourceId = "R7", .role = UseRole::Write, .range = mip(1)}}},
        {.id = "P11",
         .label = "bloom.up2",
         .kind = PassKind::Compute,
         .uses = {{.resourceId = "R6", .role = UseRole::Read, .range = mip(0)},
                  {.resourceId = "R7", .role = UseRole::Read, .range = mip(1)},
                  {.resourceId = "R7", .role = UseRole::Write, .range = mip(0)}}},
        {.id = "P12",
         .label = "composite",
         .kind = PassKind::Raster,
         .uses = {{.resourceId = "R2", .role = UseRole::Read},
                  {.resourceId = "R7", .role = UseRole::Read, .range = mip(0)},
                  {.resourceId = "R5", .role = UseRole::Read},
                  {.resourceId = "R8", .role = UseRole::ColorAttachment}}},
        {.id = "P13",
         .label = "readback",
         .kind = PassKind::Copy,
         .uses = {{.resourceId = "R8", .role = UseRole::CopySource},
                  {.resourceId = "R9", .role = UseRole::CopyDestination}}},
    };
}

//======================================================================================================================
std::vector<SinkDeclaration> buildSinks() {
    return {
        {.resourceId = "R9", .kind = SinkKind::Readback},
        {.resourceId = "R5", .kind = SinkKind::Export},
    };
}

} // namespace

//======================================================================================================================
const RepresentativeGraph& representativeGraph() {
    static const RepresentativeGraph graph{.textures = buildTextures(),
                                           .buffers = buildBuffers(),
                                           .passes = buildPasses(),
                                           .sinks = buildSinks()};
    return graph;
}

//======================================================================================================================
const std::vector<std::string>& expectedScheduleOrder() {
    // No culling: every pass reaches R9 (readback) or R5 (export) through the version chain the
    // resource table above encodes, and every dependency edge points from an earlier-declared pass
    // to a later one, so compile()'s topological order -- declaration order as its tie-break --
    // answers exactly the declared P01..P13 order.
    static const std::vector<std::string> order = {"P01", "P02", "P03", "P04", "P05", "P06", "P07",
                                                   "P08", "P09", "P10", "P11", "P12", "P13"};
    return order;
}

//======================================================================================================================
const std::string& expectedGraphDump() {
    // Generated once by declaring representativeGraph() to the production lmx::render::RenderGraph
    // over imported stand-ins for every resource above (NoApiBench --check-manifest's
    // declareToRenderGraph), compiling, and recording lmx::render::GraphDump's deterministic text
    // output verbatim (Render/GraphDump.h). The consistency test re-derives that same dump on every
    // run and compares it byte-for-byte against this string; the two can only disagree if the
    // manifest above, RenderGraph's compiler, or GraphDump's renderer changed since this was
    // captured.
    static const std::string dump = R"__(render-graph frame 0
resources
  r0 texture "shadow" D32Float
  r1 texture "sceneColor" RGBA16Float
  r2 texture "sceneDepth" D32Float
  r3 texture "bloomA" RGBA16Float
  r4 texture "bloomB" RGBA16Float
  r5 texture "out" RGBA8Unorm
  r6 texture "material0.emissive" RGBA8Unorm_sRGB
  r7 buffer "histogram"
  r8 buffer "exposure"
  r9 buffer "readback"
  r10 buffer "emissiveStaging"
sinks
  readback r9 v1
  export r8 v1
passes
  p0 copy "fill.histogram"
    copy destination r7 v0
  p1 copy "upload.emissive"
    copy source r10 v0
    copy destination r6 v0 mips[0..0] layers[0..]
  p2 raster "shadow"
    depth attachment r0 v0 mips[0..] layers[0..]
  p3 raster "scene"
    read r0 v1 mips[0..] layers[0..]
    read r6 v1 mips[0..] layers[0..]
    color attachment r1 v0 mips[0..] layers[0..]
    depth attachment r2 v0 mips[0..] layers[0..]
  p4 compute "histogram.accumulate"
    shader read r1 v1 mips[0..] layers[0..]
    shader read r8 v0
    write r7 v1
  p5 compute "histogram.resolve"
    read r7 v2
    write r8 v0
  p6 compute "bloom.threshold"
    shader read r1 v1 mips[0..] layers[0..]
    write r3 v0 mips[0..0] layers[0..]
  p7 compute "bloom.down1"
    read r3 v1 mips[0..0] layers[0..]
    write r3 v1 mips[1..1] layers[0..]
  p8 compute "bloom.down2"
    read r3 v2 mips[1..1] layers[0..]
    write r3 v2 mips[2..2] layers[0..]
  p9 compute "bloom.up1"
    read r3 v3 mips[1..1] layers[0..]
    read r3 v3 mips[2..2] layers[0..]
    write r4 v0 mips[1..1] layers[0..]
  p10 compute "bloom.up2"
    read r3 v3 mips[0..0] layers[0..]
    read r4 v1 mips[1..1] layers[0..]
    write r4 v1 mips[0..0] layers[0..]
  p11 raster "composite"
    read r1 v1 mips[0..] layers[0..]
    read r4 v2 mips[0..0] layers[0..]
    read r8 v1
    color attachment r5 v0 mips[0..] layers[0..]
  p12 copy "readback"
    copy source r5 v1 mips[0..] layers[0..]
    copy destination r9 v0
culled
transitions
  before p3 texture r0 mips[0..] layers[0..] RenderTarget -> ShaderRead
  before p3 texture r6 mips[0..0] layers[0..] CopyDestination -> ShaderRead
  before p4 texture r1 mips[0..] layers[0..] RenderTarget -> ShaderRead
  before p4 buffer r8 StorageWrite -> ShaderRead
  before p4 buffer r7 CopyDestination -> StorageWrite
  before p5 buffer r7 StorageWrite -> StorageRead
  before p5 buffer r8 StorageWrite -> StorageWrite
  before p5 buffer r8 ShaderRead -> StorageWrite
  before p7 texture r3 mips[0..0] layers[0..] StorageWrite -> StorageRead
  before p8 texture r3 mips[1..1] layers[0..] StorageWrite -> StorageRead
  before p9 texture r3 mips[2..] layers[0..] StorageWrite -> StorageRead
  before p10 texture r4 mips[1..1] layers[0..] StorageWrite -> StorageRead
  before p11 texture r1 mips[0..] layers[0..] RenderTarget -> ShaderRead
  before p11 texture r4 mips[0..0] layers[0..] StorageWrite -> ShaderRead
  before p11 buffer r8 StorageWrite -> ShaderRead
  before p12 texture r5 mips[0..] layers[0..] RenderTarget -> CopySource
transients
memory
  pooling on requested 0 high-water 0 saved 0
)__";
    return dump;
}

} // namespace lmx::noapi::workload
