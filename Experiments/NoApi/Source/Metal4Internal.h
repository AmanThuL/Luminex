//----------------------------------------------------------------------------------------------------------------------
/// @file Metal4Internal.h
/// @brief Defines the Metal 4 objects behind the prototype's opaque handles and the shared helpers.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "NoApi/NoApi.h"

#include "Core/Assert.h"

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace lmx::noapi {

/// Argument-table index the vertex stage's root address occupies, and the compute root index.
///
/// Metal 4 has no per-draw root parameter: the address is argument-table state, so the prototype
/// pins one index per stage and every draw or dispatch writes it. Shaders declare the matching
/// `[[buffer(n)]]`; the whole shader-visible ABI is these three indices.
inline constexpr uint32_t kVertexRootBindIndex = 0;

/// Argument-table index the bindless table's address occupies.
inline constexpr uint32_t kBindlessTableBindIndex = 1;

/// Argument-table index the pixel stage's root address occupies.
inline constexpr uint32_t kPixelRootBindIndex = 2;

/// Argument-table index reserved for the prototype's own split-barrier signal kernel.
inline constexpr uint32_t kInternalRootBindIndex = 3;

/// Buffer bind slots an argument table is created with.
inline constexpr uint32_t kMaxBufferBindCount = 4;

/// Bytes one bindless table slot occupies, which is `sizeof(MTL::ResourceID)`.
inline constexpr uint32_t kBindlessSlotStride = 8;

/// Largest bindless table the prototype accepts, from Metal's tier-2 argument-buffer limit.
inline constexpr uint32_t kMaxBindlessSlots = 500'000;

/// Color attachments one render pass may declare.
inline constexpr uint32_t kMaxColorAttachments = 8;

/// Threads per threadgroup every compute dispatch uses.
///
/// The model's `dispatch` carries only threadgroup counts and its pipeline descriptor carries no
/// shape, while Metal requires one at dispatch. The prototype pins this shape and shaders are
/// written against it; the deviation is recorded in the evidence document.
inline constexpr uint32_t kThreadgroupWidth = 8;

/// @copydoc kThreadgroupWidth
inline constexpr uint32_t kThreadgroupHeight = 8;

/// Longest the prototype waits for GPU progress before treating the device as wedged.
inline constexpr uint64_t kGpuTimeoutMs = 10'000;

/// Stages a render command encoder can consume a dependency in.
inline constexpr MTL::Stages kRenderStages = MTL::StageVertex | MTL::StageFragment;

/// Stages a compute command encoder can consume a dependency in, copies included.
///
/// Metal 4 has no blit encoder: copies and fills are recorded on a compute encoder, and the public
/// headers do not say whether they retire in `StageBlit` or `StageDispatch`. Naming both is correct
/// under either answer.
inline constexpr MTL::Stages kComputeStages = MTL::StageDispatch | MTL::StageBlit;

/// Copies `text` into an owning `NS::String`.
NS::SharedPtr<NS::String> makeString(std::string_view text);

/// Renders an `NS::Error` as a diagnostic string, or a fixed placeholder when it is null.
std::string describe(const NS::Error* error);

/// Builds the unexpected side of a `Result` from a code and a message.
std::unexpected<Error> fail(ErrorCode code, std::string message);

/// Translates a prototype format to its Metal pixel format.
MTL::PixelFormat toMTL(Format format);

/// Translates a prototype comparison to its Metal compare function.
MTL::CompareFunction toMTL(CompareOp op);

/// Translates a prototype stage set to the Metal queue stages that perform it.
MTL::Stages toStages(Stage stages);

/// Returns the extra consumer stages a hazard set implies.
MTL::Stages hazardStages(Hazard hazards);

/// Reports whether `value` is a power of two.
constexpr bool isPowerOfTwo(uint64_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

/// Holds one `allocate` result together with the Metal objects that back its address range.
///
/// Shared and readback allocations are one `MTL::Buffer` each. A private allocation is a placement
/// heap plus a cover buffer spanning it: the heap is what a texture is placed into, and the cover
/// buffer is what supplies the base GPU address and resolves copy commands back to a `MTL::Buffer`.
struct AllocationRecord {
    GpuAddress base = kNullAddress;       ///< First GPU address of the allocation.
    uint64_t size = 0;                    ///< Byte size the caller asked for.
    uint64_t allocatedSize = 0;           ///< Bytes Metal reserved, including alignment padding.
    uint64_t bufferOffset = 0;            ///< Bytes from the buffer's own base to `base`.
    MemoryKind kind = MemoryKind::Shared; ///< Memory kind the allocation was created with.
    void* cpu = nullptr;                  ///< Mapped host address, or null for private memory.
    NS::SharedPtr<MTL::Buffer> buffer; ///< Storage buffer, or the cover buffer of a private heap.
    NS::SharedPtr<MTL::Heap> heap;     ///< Placement heap backing a private allocation.
    uint32_t placedTextures = 0;       ///< Textures currently placed inside the allocation.
    bool resident = false; ///< Whether the allocation is currently in the residency set.
};

/// Names a `MTL::Buffer` plus the byte offset an address resolved to inside it.
struct AddressResolution {
    MTL::Buffer* buffer = nullptr; ///< Buffer the address lives in.
    uint64_t offset = 0;           ///< Byte offset of the address inside `buffer`.
    uint64_t remaining = 0;        ///< Bytes from the address to the end of the allocation.
};

/// One bindless table slot's CPU-side state.
struct BindlessSlot {
    uint32_t generation = 0;               ///< Bumped on every write and clear.
    const Texture* texture = nullptr;      ///< Texture the slot currently names, if any.
    const Sampler* sampler = nullptr;      ///< Sampler the slot currently names, if any.
    NS::SharedPtr<MTL::Texture> viewOwner; ///< Keeps a created texture view alive while referenced.
};

/// Records a `signalAfter` so that a later `waitBefore` can be checked against it.
struct PendingSignal {
    GpuAddress counter = kNullAddress; ///< Counter address the signal targeted.
    uint64_t value = 0;                ///< Value the signal published.
    SignalOp op = SignalOp::Set;       ///< Operation the signal applied.
};

/// Counts the argument-table traffic one command buffer produced.
///
/// M5.1 Stage 4 (spec section 9's binding-traffic and barrier dimensions): every field is a plain
/// integer bumped at the one choke point that performs the traffic it counts (`bindAddress` for
/// `setAddressCalls`, `pushRoot` for `rootCalls`/`rootBytes`, the barrier emission points for
/// `barrierCalls`), so counting never adds measurable work inside a timed region.
struct BindingStats {
    uint64_t setAddressCalls = 0; ///< Every `MTL4::ArgumentTable::setAddress` the prototype made.
    uint64_t rootCalls = 0;       ///< Every `pushRoot` call.
    uint64_t rootBytes = 0;       ///< Bytes `pushRoot` copied into the frame allocator.
    uint64_t barrierCalls = 0;    ///< Barrier primitives emitted onto an encoder.
};

//======================================================================================================================
struct Queue {
    Device* device = nullptr; ///< Device that owns the queue.
};

//======================================================================================================================
struct Texture {
    NS::SharedPtr<MTL::Texture> handle;  ///< Metal texture object.
    TextureDesc desc{};                  ///< Descriptor the texture was created with.
    std::string label;                   ///< Owned storage behind `desc.label`.
    GpuAddress placement = kNullAddress; ///< Address the texture was placed at.
};

//======================================================================================================================
struct Sampler {
    NS::SharedPtr<MTL::SamplerState> handle; ///< Metal sampler state.
};

//======================================================================================================================
struct BindlessTable {
    NS::SharedPtr<MTL::Buffer> storage; ///< Table memory holding one `MTL::ResourceID` per slot.
    uint32_t slotCount = 0;             ///< Slots the table was created with.
    std::vector<BindlessSlot> slots;    ///< CPU-side validation state, one entry per slot.
    // M5.1 Stage 4 (spec section 9's binding-traffic dimension): cumulative since creation, bumped
    // at writeTextureSlot/writeSamplerSlot's one choke point each.
    uint64_t writeCalls = 0; ///< Every writeTextureSlot/writeSamplerSlot call.
    uint64_t writeBytes = 0; ///< Bytes written, at `kBindlessSlotStride` per slot.
};

//======================================================================================================================
struct Pipeline {
    bool graphics = false;                            ///< Whether this is a graphics pipeline.
    NS::SharedPtr<MTL::RenderPipelineState> render;   ///< Graphics pipeline state.
    NS::SharedPtr<MTL::ComputePipelineState> compute; ///< Compute pipeline state.
    MTL::PrimitiveType primitive = MTL::PrimitiveTypeTriangle; ///< Baked primitive assembly rule.
    std::array<Format, kMaxColorAttachments> colorFormats{};   ///< Baked color attachment formats.
    uint32_t colorCount = 0;                ///< Color attachments the pipeline bakes.
    Format depthFormat = Format::Undefined; ///< Baked depth attachment format.
    std::string label;                      ///< Debug label, for assert messages.
};

//======================================================================================================================
struct DepthStencilState {
    NS::SharedPtr<MTL::DepthStencilState> handle; ///< Metal depth-stencil state.
};

//======================================================================================================================
struct Semaphore {
    NS::SharedPtr<MTL::SharedEvent> handle; ///< Metal shared event carrying the timeline.
};

//======================================================================================================================
struct ResidencySet {
    Device* device = nullptr; ///< Device whose single Metal residency set this handle names.
};

//======================================================================================================================
struct CommandBuffer {
    Device* device = nullptr;                        ///< Device that owns the context.
    NS::SharedPtr<MTL4::CommandAllocator> allocator; ///< Allocator backing this context's commands.
    NS::SharedPtr<MTL4::CommandBuffer> buffer;       ///< Metal command buffer being recorded.
    NS::SharedPtr<MTL4::ArgumentTable> argumentTable; ///< Argument table every encoder binds.
    LinearAllocator* rootAllocator = nullptr;         ///< Frame storage `pushRoot` suballocates.
    std::string label;                                ///< Debug label of the current recording.
    uint64_t retireValue = 0;                         ///< Timeline value that retires this context.
    bool recording = false;                           ///< Between `beginCommands` and `submit`.
    bool ended = false;                               ///< `endCommands` has run.

    NS::SharedPtr<MTL4::RenderCommandEncoder> render;   ///< Open render encoder, if any.
    NS::SharedPtr<MTL4::ComputeCommandEncoder> compute; ///< Open compute encoder, if any.
    bool encoderHasWork = false;                        ///< The open encoder has recorded commands.
    bool pipelineDirty = false; ///< An internal dispatch replaced `pipeline`.
    MTL::ComputePipelineState* boundCompute = nullptr; ///< Compute state the open encoder holds.
    const Pipeline* pipeline = nullptr;                ///< Pipeline the next draw or dispatch uses.
    const BindlessTable* table = nullptr;              ///< Table `setBindlessTable` published.
    uint32_t debugGroupDepth = 0;                      ///< Open debug groups.

    bool renderPassOpen = false;                                 ///< A render pass is open.
    std::array<Format, kMaxColorAttachments> passColorFormats{}; ///< Open pass's color formats.
    uint32_t passColorCount = 0;                                 ///< Open pass's color attachments.
    Format passDepthFormat = Format::Undefined;                  ///< Open pass's depth format.

    MTL::Stages pendingProducer{};               ///< Producer stages awaiting a consumer encoder.
    MTL::Stages pendingConsumer{};               ///< Consumer stages no open encoder could serve.
    MTL4::VisibilityOptions pendingVisibility{}; ///< Visibility options of the pending barrier.

    std::vector<PendingSignal> signals; ///< Split-barrier signals recorded in this command buffer.
    BindingStats stats{};               ///< Binding traffic this recording produced.
};

//======================================================================================================================
struct Device {
    NS::SharedPtr<MTL::Device> mtl;             ///< Metal device.
    NS::SharedPtr<MTL4::CommandQueue> queue;    ///< The device's single command queue.
    NS::SharedPtr<MTL4::Compiler> compiler;     ///< Compiler every pipeline is created through.
    NS::SharedPtr<MTL::ResidencySet> residency; ///< The device's single residency set.
    NS::SharedPtr<MTL::SharedEvent> timeline;   ///< Submission timeline retiring pooled contexts.
    NS::SharedPtr<MTL::ComputePipelineState> signalKernel; ///< Split-barrier counter-store kernel.
    bool signalKernelTried = false; ///< Whether the signal kernel has been compiled.

    Capabilities caps{}; ///< Values `capabilities` reports.
    Queue queueHandle{}; ///< Handle `mainQueue` returns.
    std::string name;    ///< Device name, for diagnostics.

    std::vector<AllocationRecord> allocations; ///< Live allocations, sorted by base address.
    uint64_t residentBytes = 0;                ///< Bytes currently declared resident.
    bool residencyDirty = false;               ///< Residency changes await a commit.

    std::vector<std::unique_ptr<CommandBuffer>> contexts; ///< Pooled recording contexts.
    uint64_t submissions = 0;                             ///< Submissions committed to the queue.

    uint32_t liveTextures = 0;      ///< Textures created and not yet destroyed.
    uint32_t liveSamplers = 0;      ///< Samplers created and not yet destroyed.
    uint32_t livePipelines = 0;     ///< Pipelines created and not yet destroyed.
    uint32_t liveDepthStates = 0;   ///< Depth-stencil states created and not yet destroyed.
    uint32_t liveSemaphores = 0;    ///< Semaphores created and not yet destroyed.
    BindlessTable* table = nullptr; ///< The device's single bindless table, if created.
    ResidencySet* residencyHandle = nullptr; ///< The device's single residency-set handle.
};

/// Returns the allocation record containing `address`, or null when no live allocation does.
const AllocationRecord* findAllocation(const Device* device, GpuAddress address);

/// Returns the mutable allocation record containing `address`, or null when no live allocation
/// does.
AllocationRecord* findAllocation(Device* device, GpuAddress address);

/// Resolves `address` to the buffer and offset a copy command needs.
///
/// Asserts when the address lies outside every live allocation, which is the diagnosable form of
/// the page fault the model accepts as the failure mode.
AddressResolution resolveAddress(const Device* device, GpuAddress address, std::string_view what);

/// Adds a Metal allocation to the device's residency set and marks it uncommitted.
void addResidency(Device* device, const MTL::Allocation* allocation, uint64_t bytes);

/// Removes a Metal allocation from the device's residency set and marks it uncommitted.
void removeResidency(Device* device, const MTL::Allocation* allocation, uint64_t bytes);

/// Commits pending residency changes when there are any.
void commitPendingResidency(Device* device);

/// Blocks until every submission committed so far has completed.
void drainDevice(Device* device);

/// Asserts that no submission is still in flight, which every destruction contract requires.
///
/// The interface states the contract per object; the prototype checks the device-wide form, which
/// is conservative, O(1), and never accepts a use-after-free the per-object rule would reject.
void assertNoWorkInFlight(const Device* device, std::string_view what);

} // namespace lmx::noapi
