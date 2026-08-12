//----------------------------------------------------------------------------------------------------------------------
/// @file RepresentativeGraph.h
/// @brief Declares RepresentativeGraph for the NoApi experiment.
//----------------------------------------------------------------------------------------------------------------------

/// @details Declares the frozen thirteen-pass representative graph (spec section 6): its resource
///        table, pass table, sinks, and the expected compiled schedule a consistency test checks
///        both encoders' adapters against.

#pragma once
#include "Workload/Types.h"

#include <string>
#include <vector>

namespace lmx::experimental::noapi::workload {

/// The frozen scene extent (spec section 6: "scene extent 1024x1024").
inline constexpr uint32_t kSceneWidth = 1024;
inline constexpr uint32_t kSceneHeight = 1024;
inline constexpr uint32_t kShadowExtent = 2048;
inline constexpr uint32_t kBloomExtent = 512;
inline constexpr uint32_t kBloomMipCount = 3;
inline constexpr uint32_t kHistogramBinCount = 256;
inline constexpr uint64_t kHistogramBufferSize = kHistogramBinCount * sizeof(uint32_t);
inline constexpr uint64_t kReadbackBufferSize = 4ull * 1024 * 1024;
inline constexpr uint64_t kEmissiveRingSlotSize = 16ull * 1024;
inline constexpr uint32_t kEmissiveRingSlotCount = 3;
inline constexpr uint32_t kMaterialCount = 64;
inline constexpr uint32_t kMaterialTextureSize = 64;
inline constexpr uint32_t kDrawCount = 1024;
inline constexpr uint32_t kDrawGridSize = 32; // 32 x 32 == kDrawCount
inline constexpr uint32_t kCorrectnessFrameCount = 32;

/// Id of the one material whose emissive texture (t6) P02 re-uploads every frame (spec section 6:
/// "material 0's emissive texture is re-uploaded every frame via P02"). Tracked as its own
/// imported resource -- outside the frozen R1-R10 letter scheme -- because it is the one Materials
/// texture a pass writes mid-frame, and the reupload/read hazard between P02 and P04 needs a
/// resource the graph can order across, exactly as R1-R10 do. Every other material texture is
/// read-only for the whole frame and is bound directly at execute time without a graph
/// declaration, mirroring how production's ScenePass binds t0/t1/t4/t5/t6/t7/t8/t9 without routing
/// them through PassDesc (Source/Render/Renderer.cpp's declarePasses: only the shadow map, t3,
/// gets a textureReads declaration; the rest are plain execute()-time binds because nothing writes
/// them again this frame).
inline constexpr const char* kMaterialZeroEmissiveId = "material0.emissive";

/// Everything spec section 6 fixes about the representative graph: its resources, its thirteen
/// passes, and its two sinks.
struct RepresentativeGraph {
    std::vector<TextureResource> textures; ///< R1-R3, R6-R8, and material0's tracked emissive.
    std::vector<BufferResource> buffers;   ///< R4, R5, R9, R10.
    std::vector<PassDeclaration> passes;   ///< P01-P13, in declaration order.
    std::vector<SinkDeclaration> sinks;    ///< R9 readback and the persistent R5 export.
};

/// The one frozen instance (spec section 6). Stateless and returned by const reference; safe to
/// call repeatedly and share across adapters.
const RepresentativeGraph& representativeGraph();

/// The pass ids compile() must answer with, in schedule order -- exactly P01..P13 with no
/// culling, since every declared pass reaches one of the two sinks through the version chain the
/// resource table above encodes (recorded here rather than derived so a consistency-test failure
/// names what was expected, not just that something changed).
const std::vector<std::string>& expectedScheduleOrder();

/// The deterministic text `lmx::render::GraphDump::dumpCompiledFrame` produces for the compiled
/// representative graph, declared over imported stand-ins for every resource above in the order
/// `representativeGraph()` lists them. This is the manifest's stored expectation for the schedule
/// and every derived barrier (spec section 6: "a consistency test declares the same graph to the
/// production RenderGraph and asserts that the manifest's schedule and barrier expectations match
/// the compiled CompiledFrameRecord"); `NoApiBench --check-manifest` is what declares the graph and
/// produces the dump this compares against.
const std::string& expectedGraphDump();

} // namespace lmx::experimental::noapi::workload
