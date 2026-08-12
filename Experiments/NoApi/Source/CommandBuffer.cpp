//----------------------------------------------------------------------------------------------------------------------
/// @file CommandBuffer.cpp
/// @brief Implements flat command recording, address-based binding, barriers, and submission.
//----------------------------------------------------------------------------------------------------------------------

#include "Metal4Internal.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <string>
#include <vector>

namespace lmx::experimental::noapi {
namespace {

// The kernel behind `signalAfter`'s atomic operations. Metal has no 64-bit device atomics on the
// prototype's target, so the counter's low word carries the value and the high word is zeroed;
// values above 32 bits are refused rather than truncated.
constexpr std::string_view kSignalKernelSource = R"(
#include <metal_stdlib>
using namespace metal;

struct SignalRoot {
    device atomic_uint* low;
    device uint* high;
    uint value;
    uint op;
};

kernel void lmxNoApiSignalCounter(constant SignalRoot& root [[buffer(3)]]) {
    root.high[0] = 0u;
    if (root.op == 0u) {
        atomic_store_explicit(root.low, root.value, memory_order_relaxed);
    } else if (root.op == 1u) {
        atomic_fetch_max_explicit(root.low, root.value, memory_order_relaxed);
    } else {
        atomic_fetch_or_explicit(root.low, root.value, memory_order_relaxed);
    }
}
)";

// CPU mirror of the kernel's root block; the declaration is the whole layout contract.
struct SignalRootBlock {
    uint64_t low = 0;
    uint64_t high = 0;
    uint32_t value = 0;
    uint32_t op = 0;
};

//======================================================================================================================
uint32_t indexSizeOf(IndexKind kind) {
    return kind == IndexKind::Uint16 ? 2u : 4u;
}

//======================================================================================================================
MTL::IndexType toMTL(IndexKind kind) {
    return kind == IndexKind::Uint16 ? MTL::IndexTypeUInt16 : MTL::IndexTypeUInt32;
}

//======================================================================================================================
MTL::LoadAction toMTL(LoadAction action) {
    switch (action) {
    case LoadAction::Load:
        return MTL::LoadActionLoad;
    case LoadAction::Clear:
        return MTL::LoadActionClear;
    case LoadAction::DontCare:
        break;
    }
    return MTL::LoadActionDontCare;
}

//======================================================================================================================
MTL::StoreAction toMTL(StoreAction action) {
    return action == StoreAction::Store ? MTL::StoreActionStore : MTL::StoreActionDontCare;
}

//======================================================================================================================
MTL::CullMode toMTL(CullMode mode) {
    switch (mode) {
    case CullMode::Front:
        return MTL::CullModeFront;
    case CullMode::Back:
        return MTL::CullModeBack;
    case CullMode::None:
        break;
    }
    return MTL::CullModeNone;
}

//======================================================================================================================
bool satisfies(uint64_t observed, CompareOp op, uint64_t required) {
    switch (op) {
    case CompareOp::Never:
        return false;
    case CompareOp::Less:
        return observed < required;
    case CompareOp::Equal:
        return observed == required;
    case CompareOp::LessEqual:
        return observed <= required;
    case CompareOp::Greater:
        return observed > required;
    case CompareOp::NotEqual:
        return observed != required;
    case CompareOp::GreaterEqual:
        return observed >= required;
    case CompareOp::Always:
        break;
    }
    return true;
}

//======================================================================================================================
// Every argument-table write in the prototype passes through here. Root data on Metal 4 is encoder
// state rather than a draw parameter, so this is where the model's "the draw carries its address"
// claim turns into real API traffic -- and the single place instrumentation has to count.
void bindAddress(CommandBuffer* commands, uint32_t index, GpuAddress address) {
    commands->argumentTable->setAddress(address, index);
    commands->stats.setAddressCalls += 1;
}

//======================================================================================================================
// Emits the barrier a consumer encoder owes, restricted to the stages that encoder can consume in.
// Whatever the encoder cannot consume stays pending for the next one.
void flushPendingBarrier(CommandBuffer* commands, MTL4::CommandEncoder* encoder,
                         MTL::Stages encoderStages) {
    const MTL::Stages consumable = commands->pendingConsumer & encoderStages;
    if (consumable == MTL::Stages{}) {
        return;
    }
    // afterQueueStages reaches back across every earlier encoder of the queue, which is what lets
    // a barrier recorded between passes be emitted by the pass that consumes it.
    encoder->barrierAfterQueueStages(commands->pendingProducer, consumable,
                                     commands->pendingVisibility);
    commands->stats.barrierCalls += 1;
    commands->pendingConsumer &= ~consumable;
    if (commands->pendingConsumer == MTL::Stages{}) {
        commands->pendingProducer = MTL::Stages{};
        commands->pendingVisibility = MTL4::VisibilityOptions{};
    }
}

//======================================================================================================================
void closeComputeEncoder(CommandBuffer* commands) {
    if (!commands->compute) {
        return;
    }
    commands->compute->endEncoding();
    commands->compute.reset();
}

//======================================================================================================================
MTL4::ComputeCommandEncoder* ensureComputeEncoder(CommandBuffer* commands, std::string_view what) {
    LMX_ASSERT(commands->recording, std::format("{}: this command buffer is not recording", what));
    LMX_ASSERT(!commands->renderPassOpen,
               std::format("{}: a render pass is open -- copies, dispatches, and fills are "
                           "recorded outside one",
                           what));
    if (commands->compute) {
        return commands->compute.get();
    }
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
    commands->compute = NS::RetainPtr(commands->buffer->computeCommandEncoder());
    LMX_ASSERT(commands->compute,
               std::format("{}: failed to create a compute command encoder", what));
    commands->compute->setLabel(makeString(commands->label + ".compute").get());
    commands->encoderHasWork = false;
    commands->boundCompute = nullptr;
    flushPendingBarrier(commands, commands->compute.get(), kComputeStages);
    commands->compute->setArgumentTable(commands->argumentTable.get());
    return commands->compute.get();
}

//======================================================================================================================
// Emits a dependency onto an open encoder. Metal 4 splits the two scopes rather than nesting them:
// queue stages name work from *earlier* encoders, encoder stages name commands recorded into this
// one. A producer that could be in both therefore needs both, and neither alone is a superset.
void emitBarrier(CommandBuffer* commands, MTL4::CommandEncoder* encoder, MTL::Stages encoderStages,
                 MTL::Stages producer, MTL::Stages consumable) {
    const MTL4::VisibilityOptions visibility = MTL4::VisibilityOptionDevice;
    const MTL::Stages inEncoder = producer & encoderStages;
    const MTL::Stages outsideEncoder = producer & ~encoderStages;

    if (inEncoder != MTL::Stages{} && commands->encoderHasWork) {
        encoder->barrierAfterEncoderStages(inEncoder, consumable, visibility);
        commands->stats.barrierCalls += 1;
    }
    if (outsideEncoder != MTL::Stages{} || !commands->encoderHasWork) {
        encoder->barrierAfterQueueStages(producer, consumable, visibility);
        commands->stats.barrierCalls += 1;
    }
}

//======================================================================================================================
// Records a stage dependency, emitting it now when an open encoder can consume it and deferring
// the rest to the encoder that can.
void recordBarrier(CommandBuffer* commands, MTL::Stages producer, MTL::Stages consumer) {
    if (producer == MTL::Stages{} || consumer == MTL::Stages{}) {
        return;
    }
    MTL::Stages remaining = consumer;

    if (commands->render) {
        const MTL::Stages consumable = remaining & kRenderStages;
        if (consumable != MTL::Stages{}) {
            emitBarrier(commands, commands->render.get(), kRenderStages, producer, consumable);
            remaining &= ~consumable;
        }
    } else if (commands->compute) {
        const MTL::Stages consumable = remaining & kComputeStages;
        if (consumable != MTL::Stages{}) {
            emitBarrier(commands, commands->compute.get(), kComputeStages, producer, consumable);
            remaining &= ~consumable;
        }
    }

    if (remaining == MTL::Stages{}) {
        return;
    }
    commands->pendingProducer |= producer;
    commands->pendingConsumer |= remaining;
    // Device visibility is the only scope Metal 4 offers for making writes readable; there is no
    // descriptor-cache option, so Hazard::Descriptors rides on this same flag.
    commands->pendingVisibility |= MTL4::VisibilityOptionDevice;
}

//======================================================================================================================
// Sets compute state only when it differs from what the encoder already holds. The prototype's own
// signal kernel is what makes a repeat possible, and Metal's validation layer reports a redundant
// set as a warning.
void setComputeState(CommandBuffer* commands, MTL4::ComputeCommandEncoder* encoder,
                     MTL::ComputePipelineState* state) {
    if (commands->boundCompute == state) {
        return;
    }
    encoder->setComputePipelineState(state);
    commands->boundCompute = state;
}

//======================================================================================================================
// Re-sets the caller's compute pipeline when the prototype's own signal kernel displaced it.
void restorePipeline(CommandBuffer* commands, MTL4::ComputeCommandEncoder* encoder) {
    if (!commands->pipelineDirty) {
        return;
    }
    LMX_ASSERT(commands->pipeline != nullptr && !commands->pipeline->graphics,
               "dispatch: no compute pipeline is set");
    setComputeState(commands, encoder, commands->pipeline->compute.get());
    commands->pipelineDirty = false;
}

//======================================================================================================================
MTL::ComputePipelineState* signalKernel(Device* device) {
    if (device->signalKernelTried) {
        return device->signalKernel.get();
    }
    device->signalKernelTried = true;
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    auto libraryDesc = NS::TransferPtr(MTL4::LibraryDescriptor::alloc()->init());
    auto options = NS::TransferPtr(MTL::CompileOptions::alloc()->init());
    options->setLanguageVersion(MTL::LanguageVersion4_0);
    libraryDesc->setOptions(options.get());
    libraryDesc->setName(makeString("lmx.noapi.signal").get());
    libraryDesc->setSource(makeString(kSignalKernelSource).get());

    NS::Error* error = nullptr;
    NS::SharedPtr<MTL::Library> library =
        NS::TransferPtr(device->compiler->newLibrary(libraryDesc.get(), &error));
    if (!library) {
        return nullptr;
    }
    library->setLabel(makeString("lmx.noapi.signal").get());

    auto function = NS::TransferPtr(MTL4::LibraryFunctionDescriptor::alloc()->init());
    function->setLibrary(library.get());
    function->setName(makeString("lmxNoApiSignalCounter").get());

    auto pipelineDesc = NS::TransferPtr(MTL4::ComputePipelineDescriptor::alloc()->init());
    pipelineDesc->setComputeFunctionDescriptor(function.get());
    pipelineDesc->setLabel(makeString("lmx.noapi.signal.counter").get());

    error = nullptr;
    device->signalKernel = NS::TransferPtr(device->compiler->newComputePipelineState(
        pipelineDesc.get(), /*compilerTaskOptions=*/nullptr, &error));
    return device->signalKernel.get();
}

} // namespace

//======================================================================================================================
CommandBuffer* beginCommands(Queue* queue, LinearAllocator* rootAllocator, std::string_view label) {
    LMX_ASSERT(queue != nullptr, "beginCommands: queue must not be null");
    LMX_ASSERT(rootAllocator != nullptr, "beginCommands: rootAllocator must not be null");
    LMX_ASSERT(!label.empty(), "beginCommands: label must not be empty");
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    Device* device = queue->device;
    const uint64_t retired = device->timeline->signaledValue();

    // A context's command allocator may only be reset once the submission that used it completed;
    // the submission timeline is the single proof, exactly as the frame ring's semaphore is for
    // root storage.
    CommandBuffer* commands = nullptr;
    for (const std::unique_ptr<CommandBuffer>& candidate : device->contexts) {
        if (!candidate->recording && candidate->retireValue <= retired) {
            commands = candidate.get();
            break;
        }
    }

    if (commands == nullptr) {
        auto created = std::make_unique<CommandBuffer>();
        created->device = device;

        NS::Error* error = nullptr;
        auto allocatorDesc = NS::TransferPtr(MTL4::CommandAllocatorDescriptor::alloc()->init());
        const std::string suffix = std::to_string(device->contexts.size());
        allocatorDesc->setLabel(makeString("lmx.noapi.allocator." + suffix).get());
        created->allocator =
            NS::TransferPtr(device->mtl->newCommandAllocator(allocatorDesc.get(), &error));
        LMX_ASSERT(created->allocator,
                   std::format("beginCommands: failed to create a command allocator: {}",
                               describe(error)));

        created->buffer = NS::TransferPtr(device->mtl->newCommandBuffer());
        LMX_ASSERT(created->buffer, "beginCommands: failed to create a command buffer");

        error = nullptr;
        auto tableDesc = NS::TransferPtr(MTL4::ArgumentTableDescriptor::alloc()->init());
        tableDesc->setMaxBufferBindCount(kMaxBufferBindCount);
        // The model binds no textures or samplers directly: everything shader-visible reaches the
        // GPU as an address or as a resource ID inside the bindless table.
        tableDesc->setMaxTextureBindCount(0);
        tableDesc->setMaxSamplerStateBindCount(0);
        tableDesc->setInitializeBindings(true);
        tableDesc->setLabel(makeString("lmx.noapi.argumentTable." + suffix).get());
        created->argumentTable =
            NS::TransferPtr(device->mtl->newArgumentTable(tableDesc.get(), &error));
        LMX_ASSERT(
            created->argumentTable,
            std::format("beginCommands: failed to create an argument table: {}", describe(error)));

        commands = created.get();
        device->contexts.push_back(std::move(created));
    }

    commands->allocator->reset();
    commands->buffer->beginCommandBuffer(commands->allocator.get());
    commands->label = std::string(label);
    commands->buffer->setLabel(makeString(commands->label).get());
    commands->rootAllocator = rootAllocator;
    commands->recording = true;
    commands->ended = false;
    commands->pipeline = nullptr;
    commands->table = nullptr;
    commands->debugGroupDepth = 0;
    commands->renderPassOpen = false;
    commands->passColorCount = 0;
    commands->passDepthFormat = Format::Undefined;
    commands->pendingProducer = MTL::Stages{};
    commands->pendingConsumer = MTL::Stages{};
    commands->pendingVisibility = MTL4::VisibilityOptions{};
    commands->pipelineDirty = false;
    commands->boundCompute = nullptr;
    commands->signals.clear();
    commands->stats = BindingStats{};
    return commands;
}

//======================================================================================================================
void endCommands(CommandBuffer* commands) {
    LMX_ASSERT(commands != nullptr, "endCommands: commands must not be null");
    LMX_ASSERT(commands->recording, "endCommands: this command buffer is not recording");
    LMX_ASSERT(!commands->renderPassOpen,
               "endCommands: a render pass is still open -- call endRenderPass first");
    LMX_ASSERT(
        commands->debugGroupDepth == 0,
        std::format("endCommands: {} debug group(s) were never popped", commands->debugGroupDepth));
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    closeComputeEncoder(commands);
    LMX_ASSERT(commands->pendingConsumer == MTL::Stages{},
               "endCommands: a barrier was recorded but no later pass consumed it");
    commands->buffer->endCommandBuffer();
    commands->ended = true;
}

//======================================================================================================================
void submit(Queue* queue, std::span<CommandBuffer* const> commands, Semaphore* signal,
            uint64_t signalValue) {
    LMX_ASSERT(queue != nullptr, "submit: queue must not be null");
    LMX_ASSERT(!commands.empty(), "submit: at least one command buffer must be submitted");
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    Device* device = queue->device;
    std::vector<const MTL4::CommandBuffer*> buffers;
    buffers.reserve(commands.size());
    for (CommandBuffer* entry : commands) {
        LMX_ASSERT(entry != nullptr, "submit: a submitted command buffer is null");
        LMX_ASSERT(entry->device == device, "submit: a command buffer belongs to another device");
        LMX_ASSERT(entry->ended,
                   std::format("submit: command buffer '{}' was never ended", entry->label));
        buffers.push_back(entry->buffer.get());
    }

    // Residency is committed before the work that dereferences the addresses is queued. Committing
    // republishes the whole set, so a caller that commits explicitly outside its timed region pays
    // nothing here.
    commitPendingResidency(device);

    device->queue->commit(buffers.data(), buffers.size());

    // The internal timeline retires pooled contexts; it is signalled first so that a caller
    // waiting on its own semaphore has also observed every context retirement.
    device->submissions += 1;
    device->queue->signalEvent(device->timeline.get(), device->submissions);
    if (signal != nullptr) {
        device->queue->signalEvent(signal->handle.get(), signalValue);
    }

    for (CommandBuffer* entry : commands) {
        entry->retireValue = device->submissions;
        entry->recording = false;
        entry->ended = false;
        entry->rootAllocator = nullptr;
    }
}

//======================================================================================================================
void pushDebugGroup(CommandBuffer* commands, std::string_view label) {
    LMX_ASSERT(commands != nullptr && commands->recording,
               "pushDebugGroup: this command buffer is not recording");
    LMX_ASSERT(!label.empty(), "pushDebugGroup: label must not be empty");
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    commands->buffer->pushDebugGroup(makeString(label).get());
    commands->debugGroupDepth += 1;
}

//======================================================================================================================
void popDebugGroup(CommandBuffer* commands) {
    LMX_ASSERT(commands != nullptr && commands->recording,
               "popDebugGroup: this command buffer is not recording");
    LMX_ASSERT(commands->debugGroupDepth > 0, "popDebugGroup: no debug group is open");

    commands->buffer->popDebugGroup();
    commands->debugGroupDepth -= 1;
}

//======================================================================================================================
GpuAddress pushRoot(CommandBuffer* commands, const void* data, uint64_t size, uint64_t alignment) {
    LMX_ASSERT(commands != nullptr && commands->recording,
               "pushRoot: this command buffer is not recording");
    LMX_ASSERT(data != nullptr && size > 0, "pushRoot: data must be non-null and non-empty");
    LMX_ASSERT(isPowerOfTwo(alignment), "pushRoot: alignment must be a power of two");
    LMX_ASSERT(alignment >= commands->device->caps.minRootDataAlignment,
               std::format("pushRoot: {}-byte alignment is below the device's {}-byte minimum",
                           alignment, commands->device->caps.minRootDataAlignment));

    const Suballocation storage = commands->rootAllocator->allocate(size, alignment);
    std::memcpy(storage.cpu, data, size);
    commands->stats.rootCalls += 1;
    commands->stats.rootBytes += size;
    return storage.gpu;
}

//======================================================================================================================
void setBindlessTable(CommandBuffer* commands, const BindlessTable* table) {
    LMX_ASSERT(commands != nullptr && commands->recording,
               "setBindlessTable: this command buffer is not recording");
    LMX_ASSERT(table != nullptr, "setBindlessTable: table must not be null");
    LMX_ASSERT(!commands->renderPassOpen,
               "setBindlessTable: the table is published outside a render pass");

    // The table is just memory, so publishing it is one more address binding rather than a heap
    // activation call.
    bindAddress(commands, kBindlessTableBindIndex, bindlessTableAddress(table));
    commands->table = table;
}

//======================================================================================================================
void copyMemory(CommandBuffer* commands, GpuAddress destination, GpuAddress source, uint64_t size) {
    LMX_ASSERT(size > 0, "copyMemory: size must be non-zero");
    const uint64_t alignment = commands->device->caps.minCopyAlignment;
    LMX_ASSERT(destination % alignment == 0 && source % alignment == 0,
               std::format("copyMemory: both addresses must satisfy the {}-byte copy alignment",
                           alignment));
    LMX_ASSERT(destination + size <= source || source + size <= destination,
               "copyMemory: the source and destination ranges overlap");

    MTL4::ComputeCommandEncoder* encoder = ensureComputeEncoder(commands, "copyMemory");
    const AddressResolution from = resolveAddress(commands->device, source, "copyMemory source");
    const AddressResolution to =
        resolveAddress(commands->device, destination, "copyMemory destination");
    LMX_ASSERT(size <= from.remaining && size <= to.remaining,
               "copyMemory: the copy reads or writes past the end of an allocation");
    // Metal 4 has no blit encoder: copies are compute-encoder commands that take a buffer plus an
    // offset, so the prototype resolves each address back to the allocation it came from.
    encoder->copyFromBuffer(from.buffer, from.offset, to.buffer, to.offset, size);
    commands->encoderHasWork = true;
}

//======================================================================================================================
void fillMemory(CommandBuffer* commands, GpuAddress destination, uint64_t size, uint8_t value) {
    LMX_ASSERT(size > 0, "fillMemory: size must be non-zero");
    const uint64_t alignment = commands->device->caps.minCopyAlignment;
    LMX_ASSERT(
        destination % alignment == 0,
        std::format("fillMemory: the address must satisfy the {}-byte copy alignment", alignment));

    MTL4::ComputeCommandEncoder* encoder = ensureComputeEncoder(commands, "fillMemory");
    const AddressResolution to = resolveAddress(commands->device, destination, "fillMemory");
    LMX_ASSERT(size <= to.remaining, "fillMemory: the fill writes past the end of an allocation");
    encoder->fillBuffer(to.buffer, NS::Range::Make(to.offset, size), value);
    commands->encoderHasWork = true;
}

//======================================================================================================================
void copyToTexture(CommandBuffer* commands, Texture* destination, const TextureRegion& region,
                   GpuAddress source, const MemoryImageLayout& sourceLayout) {
    LMX_ASSERT(destination != nullptr, "copyToTexture: destination must not be null");
    LMX_ASSERT(hasUsage(destination->desc.usage, TextureUsage::CopyDestination),
               std::format("copyToTexture: '{}' was created without TextureUsage::CopyDestination",
                           destination->desc.label));
    LMX_ASSERT(region.mipLevel < destination->desc.mipCount,
               "copyToTexture: the region names a mip level the texture does not have");
    LMX_ASSERT(region.arrayLayer < destination->desc.arrayLayers,
               "copyToTexture: the region names an array layer the texture does not have");
    LMX_ASSERT(sourceLayout.bytesPerRow > 0, "copyToTexture: bytesPerRow must be non-zero");

    MTL4::ComputeCommandEncoder* encoder = ensureComputeEncoder(commands, "copyToTexture");
    const AddressResolution from = resolveAddress(commands->device, source, "copyToTexture source");
    encoder->copyFromBuffer(
        from.buffer, from.offset, sourceLayout.bytesPerRow, sourceLayout.bytesPerImage,
        MTL::Size::Make(region.extent.width, region.extent.height, region.extent.depth),
        destination->handle.get(), region.arrayLayer, region.mipLevel,
        MTL::Origin::Make(region.origin.x, region.origin.y, region.origin.z));
    commands->encoderHasWork = true;
}

//======================================================================================================================
void copyFromTexture(CommandBuffer* commands, GpuAddress destination,
                     const MemoryImageLayout& destinationLayout, const Texture* source,
                     const TextureRegion& region) {
    LMX_ASSERT(source != nullptr, "copyFromTexture: source must not be null");
    LMX_ASSERT(hasUsage(source->desc.usage, TextureUsage::CopySource),
               std::format("copyFromTexture: '{}' was created without TextureUsage::CopySource",
                           source->desc.label));
    LMX_ASSERT(region.mipLevel < source->desc.mipCount,
               "copyFromTexture: the region names a mip level the texture does not have");
    LMX_ASSERT(region.arrayLayer < source->desc.arrayLayers,
               "copyFromTexture: the region names an array layer the texture does not have");
    LMX_ASSERT(destinationLayout.bytesPerRow > 0, "copyFromTexture: bytesPerRow must be non-zero");

    MTL4::ComputeCommandEncoder* encoder = ensureComputeEncoder(commands, "copyFromTexture");
    const AddressResolution to =
        resolveAddress(commands->device, destination, "copyFromTexture destination");
    encoder->copyFromTexture(
        source->handle.get(), region.arrayLayer, region.mipLevel,
        MTL::Origin::Make(region.origin.x, region.origin.y, region.origin.z),
        MTL::Size::Make(region.extent.width, region.extent.height, region.extent.depth), to.buffer,
        to.offset, destinationLayout.bytesPerRow, destinationLayout.bytesPerImage);
    commands->encoderHasWork = true;
}

//======================================================================================================================
void barrier(CommandBuffer* commands, Stage producer, Stage consumer, Hazard hazards) {
    LMX_ASSERT(commands != nullptr && commands->recording,
               "barrier: this command buffer is not recording");
    recordBarrier(commands, toStages(producer), toStages(consumer) | hazardStages(hazards));
}

//======================================================================================================================
void signalAfter(CommandBuffer* commands, Stage producer, GpuAddress counter, uint64_t value,
                 SignalOp op) {
    LMX_ASSERT(commands != nullptr && commands->recording,
               "signalAfter: this command buffer is not recording");
    LMX_ASSERT(counter % 8 == 0, "signalAfter: the counter address must be eight-byte aligned");

    MTL4::ComputeCommandEncoder* encoder = ensureComputeEncoder(commands, "signalAfter");
    const AddressResolution target = resolveAddress(commands->device, counter, "signalAfter");
    LMX_ASSERT(target.remaining >= sizeof(uint64_t),
               "signalAfter: the counter does not fit inside its allocation");

    // Metal has no memory-signalling primitive: the ordering is a stage barrier and the counter is
    // written by an ordinary command, so the producer must be ordered before that write.
    recordBarrier(commands, toStages(producer), kComputeStages);

    if (op == SignalOp::Set) {
        // A copy publishes the full 64-bit value exactly; no kernel and no atomics are involved.
        const Suballocation staging = commands->rootAllocator->allocate(sizeof(uint64_t), 8);
        std::memcpy(staging.cpu, &value, sizeof(value));
        const AddressResolution from =
            resolveAddress(commands->device, staging.gpu, "signalAfter staging");
        encoder->copyFromBuffer(from.buffer, from.offset, target.buffer, target.offset,
                                sizeof(uint64_t));
        commands->encoderHasWork = true;
    } else {
        MTL::ComputePipelineState* kernel = signalKernel(commands->device);
        LMX_ASSERT(kernel != nullptr,
                   "signalAfter: SignalOp::AtomicMax and SignalOp::AtomicOr need the prototype's "
                   "atomic signal kernel, which this device refused to compile");
        LMX_ASSERT(value <= 0xFFFFFFFFull,
                   std::format("signalAfter: value {} exceeds the 32 bits this prototype's atomic "
                               "signal emulation can carry -- Metal 4 has no 64-bit device atomics "
                               "on this target",
                               value));
        SignalRootBlock block{.low = counter,
                              .high = counter + 4,
                              .value = static_cast<uint32_t>(value),
                              .op = op == SignalOp::AtomicMax ? 1u : 2u};
        const Suballocation root = commands->rootAllocator->allocate(sizeof(block), 8);
        std::memcpy(root.cpu, &block, sizeof(block));
        bindAddress(commands, kInternalRootBindIndex, root.gpu);
        setComputeState(commands, encoder, kernel);
        encoder->dispatchThreadgroups(MTL::Size::Make(1, 1, 1), MTL::Size::Make(1, 1, 1));
        commands->encoderHasWork = true;
        // The caller's pipeline is encoder state this dispatch just overwrote. Restoring it here
        // would leave a pipeline set and unused whenever the next command is another signal, which
        // Metal's validation layer reports; the next dispatch restores it instead.
        commands->pipelineDirty = true;
    }

    commands->signals.push_back({.counter = counter, .value = value, .op = op});
}

//======================================================================================================================
void waitBefore(CommandBuffer* commands, Stage consumer, GpuAddress counter, uint64_t value,
                CompareOp op, Hazard hazards, uint64_t mask) {
    LMX_ASSERT(commands != nullptr && commands->recording,
               "waitBefore: this command buffer is not recording");

    // Metal 4 cannot wait on a memory value: what orders the consumer is a stage barrier against
    // the commands that wrote the counter. The comparison is therefore checked here, at record
    // time, against the signal it pairs with -- which requires that pairing to be in this command
    // buffer.
    const auto signalled =
        std::find_if(commands->signals.rbegin(), commands->signals.rend(),
                     [&](const PendingSignal& entry) { return entry.counter == counter; });
    LMX_ASSERT(signalled != commands->signals.rend(),
               std::format("waitBefore: no signalAfter for counter {:#x} was recorded earlier in "
                           "this command buffer -- the prototype's split barrier orders stages, so "
                           "it cannot wait on a value another submission will write",
                           counter));
    LMX_ASSERT(satisfies(signalled->value & mask, op, value & mask),
               std::format("waitBefore: the signal on counter {:#x} published {}, which does not "
                           "satisfy the requested comparison against {}",
                           counter, signalled->value, value));

    recordBarrier(commands, kComputeStages, toStages(consumer) | hazardStages(hazards));
}

//======================================================================================================================
void setPipeline(CommandBuffer* commands, const Pipeline* pipeline) {
    LMX_ASSERT(commands != nullptr && commands->recording,
               "setPipeline: this command buffer is not recording");
    LMX_ASSERT(pipeline != nullptr, "setPipeline: pipeline must not be null");

    if (pipeline->graphics) {
        LMX_ASSERT(commands->renderPassOpen,
                   std::format("setPipeline: graphics pipeline '{}' needs an open render pass",
                               pipeline->label));
        LMX_ASSERT(pipeline->colorCount == commands->passColorCount,
                   std::format("setPipeline: '{}' bakes {} color target(s) but the open pass "
                               "declares {}",
                               pipeline->label, pipeline->colorCount, commands->passColorCount));
        for (uint32_t index = 0; index < pipeline->colorCount; ++index) {
            LMX_ASSERT(pipeline->colorFormats[index] == commands->passColorFormats[index],
                       std::format("setPipeline: '{}' bakes a different format for color "
                                   "attachment {} than the open pass declares",
                                   pipeline->label, index));
        }
        LMX_ASSERT(pipeline->depthFormat == commands->passDepthFormat,
                   std::format("setPipeline: '{}' bakes a different depth format than the open "
                               "pass declares",
                               pipeline->label));
        commands->render->setRenderPipelineState(pipeline->render.get());
    } else {
        LMX_ASSERT(!commands->renderPassOpen,
                   std::format("setPipeline: compute pipeline '{}' cannot be set inside a render "
                               "pass",
                               pipeline->label));
        MTL4::ComputeCommandEncoder* encoder = ensureComputeEncoder(commands, "setPipeline");
        setComputeState(commands, encoder, pipeline->compute.get());
        commands->pipelineDirty = false;
    }
    commands->pipeline = pipeline;
}

//======================================================================================================================
void setDepthStencilState(CommandBuffer* commands, const DepthStencilState* state) {
    LMX_ASSERT(commands != nullptr && commands->renderPassOpen,
               "setDepthStencilState: no render pass is open");
    LMX_ASSERT(state != nullptr, "setDepthStencilState: state must not be null");
    commands->render->setDepthStencilState(state->handle.get());
}

//======================================================================================================================
void setViewport(CommandBuffer* commands, const Viewport& viewport) {
    LMX_ASSERT(commands != nullptr && commands->renderPassOpen, "setViewport: no render pass is "
                                                                "open");
    commands->render->setViewport(MTL::Viewport{viewport.x, viewport.y, viewport.width,
                                                viewport.height, viewport.minDepth,
                                                viewport.maxDepth});
}

//======================================================================================================================
void setScissor(CommandBuffer* commands, const ScissorRect& scissor) {
    LMX_ASSERT(commands != nullptr && commands->renderPassOpen, "setScissor: no render pass is "
                                                                "open");
    commands->render->setScissorRect(
        MTL::ScissorRect{scissor.x, scissor.y, scissor.width, scissor.height});
}

//======================================================================================================================
void setCullMode(CommandBuffer* commands, CullMode cull) {
    LMX_ASSERT(commands != nullptr && commands->renderPassOpen, "setCullMode: no render pass is "
                                                                "open");
    commands->render->setCullMode(toMTL(cull));
}

//======================================================================================================================
void setFrontFace(CommandBuffer* commands, Winding winding) {
    LMX_ASSERT(commands != nullptr && commands->renderPassOpen, "setFrontFace: no render pass is "
                                                                "open");
    commands->render->setFrontFacingWinding(
        winding == Winding::Clockwise ? MTL::WindingClockwise : MTL::WindingCounterClockwise);
}

//======================================================================================================================
void setDepthBias(CommandBuffer* commands, float constantBias, float slopeScale, float clamp) {
    LMX_ASSERT(commands != nullptr && commands->renderPassOpen, "setDepthBias: no render pass is "
                                                                "open");
    commands->render->setDepthBias(constantBias, slopeScale, clamp);
}

//======================================================================================================================
void setStencilReference(CommandBuffer* commands, uint32_t reference) {
    LMX_ASSERT(commands != nullptr && commands->renderPassOpen,
               "setStencilReference: no render pass is open");
    commands->render->setStencilReferenceValue(reference);
}

//======================================================================================================================
void beginRenderPass(CommandBuffer* commands, const RenderPassDesc& desc) {
    LMX_ASSERT(commands != nullptr && commands->recording,
               "beginRenderPass: this command buffer is not recording");
    LMX_ASSERT(!commands->renderPassOpen, "beginRenderPass: a render pass is already open");
    LMX_ASSERT(!desc.label.empty(), "beginRenderPass: RenderPassDesc.label must not be empty");
    LMX_ASSERT(desc.colorTargets.size() <= kMaxColorAttachments,
               "beginRenderPass: more color attachments than this prototype supports");
    LMX_ASSERT(!desc.colorTargets.empty() || desc.depth != nullptr,
               "beginRenderPass: a pass must declare at least one attachment");
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    // A render pass is a different encoder, and pass boundaries emit no barrier of their own.
    closeComputeEncoder(commands);

    auto passDesc = NS::TransferPtr(MTL4::RenderPassDescriptor::alloc()->init());
    uint32_t width = 0;
    uint32_t height = 0;

    for (size_t index = 0; index < desc.colorTargets.size(); ++index) {
        const ColorAttachment& attachment = desc.colorTargets[index];
        LMX_ASSERT(attachment.texture != nullptr,
                   "beginRenderPass: a color attachment has no texture");
        LMX_ASSERT(hasUsage(attachment.texture->desc.usage, TextureUsage::ColorAttachment),
                   std::format("beginRenderPass: '{}' was created without "
                               "TextureUsage::ColorAttachment",
                               attachment.texture->desc.label));
        LMX_ASSERT(attachment.mipLevel < attachment.texture->desc.mipCount,
                   "beginRenderPass: a color attachment names a mip level it does not have");
        MTL::RenderPassColorAttachmentDescriptor* color =
            passDesc->colorAttachments()->object(index);
        color->setTexture(attachment.texture->handle.get());
        color->setLevel(attachment.mipLevel);
        color->setSlice(attachment.arrayLayer);
        color->setLoadAction(toMTL(attachment.load));
        color->setStoreAction(toMTL(attachment.store));
        color->setClearColor(MTL::ClearColor(attachment.clearColor[0], attachment.clearColor[1],
                                             attachment.clearColor[2], attachment.clearColor[3]));
        commands->passColorFormats[index] = attachment.texture->desc.format;
        if (index == 0) {
            width = std::max(1u, attachment.texture->desc.extent.width >> attachment.mipLevel);
            height = std::max(1u, attachment.texture->desc.extent.height >> attachment.mipLevel);
        }
    }
    commands->passColorCount = static_cast<uint32_t>(desc.colorTargets.size());
    commands->passDepthFormat = Format::Undefined;

    if (desc.depth != nullptr) {
        LMX_ASSERT(desc.depth->texture != nullptr,
                   "beginRenderPass: the depth attachment has no texture");
        LMX_ASSERT(hasUsage(desc.depth->texture->desc.usage, TextureUsage::DepthStencilAttachment),
                   std::format("beginRenderPass: '{}' was created without "
                               "TextureUsage::DepthStencilAttachment",
                               desc.depth->texture->desc.label));
        MTL::RenderPassDepthAttachmentDescriptor* depth = passDesc->depthAttachment();
        depth->setTexture(desc.depth->texture->handle.get());
        depth->setLevel(desc.depth->mipLevel);
        depth->setSlice(desc.depth->arrayLayer);
        depth->setLoadAction(toMTL(desc.depth->load));
        depth->setStoreAction(toMTL(desc.depth->store));
        depth->setClearDepth(desc.depth->clearDepth);
        commands->passDepthFormat = desc.depth->texture->desc.format;
        if (width == 0) {
            width = std::max(1u, desc.depth->texture->desc.extent.width >> desc.depth->mipLevel);
            height = std::max(1u, desc.depth->texture->desc.extent.height >> desc.depth->mipLevel);
        }
    }
    passDesc->setDefaultRasterSampleCount(1);
    passDesc->setRenderTargetWidth(width);
    passDesc->setRenderTargetHeight(height);

    commands->render = NS::RetainPtr(commands->buffer->renderCommandEncoder(passDesc.get()));
    LMX_ASSERT(commands->render, "beginRenderPass: failed to create a render command encoder");
    commands->render->setLabel(makeString(desc.label).get());

    commands->encoderHasWork = false;
    flushPendingBarrier(commands, commands->render.get(), kRenderStages);
    commands->render->setArgumentTable(commands->argumentTable.get(),
                                       MTL::RenderStageVertex | MTL::RenderStageFragment);
    // No default viewport is set here. Metal derives one from the pass's render target size, which
    // the descriptor above states explicitly, and setting it again would make every caller-supplied
    // viewport a redundant state change the validation layer reports.

    commands->renderPassOpen = true;
    commands->pipeline = nullptr;
}

//======================================================================================================================
void endRenderPass(CommandBuffer* commands) {
    LMX_ASSERT(commands != nullptr && commands->renderPassOpen,
               "endRenderPass: no render pass is open");
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    commands->render->endEncoding();
    commands->render.reset();
    commands->renderPassOpen = false;
    commands->pipeline = nullptr;
}

//======================================================================================================================
void draw(CommandBuffer* commands, GpuAddress vertexRoot, GpuAddress pixelRoot,
          uint32_t vertexCount, uint32_t instanceCount) {
    LMX_ASSERT(commands != nullptr && commands->renderPassOpen, "draw: no render pass is open");
    LMX_ASSERT(commands->pipeline != nullptr && commands->pipeline->graphics,
               "draw: no graphics pipeline is set");
    LMX_ASSERT(vertexCount > 0 && instanceCount > 0,
               "draw: vertexCount and instanceCount must be non-zero");

    if (vertexRoot != kNullAddress) {
        bindAddress(commands, kVertexRootBindIndex, vertexRoot);
    }
    if (pixelRoot != kNullAddress) {
        bindAddress(commands, kPixelRootBindIndex, pixelRoot);
    }
    commands->render->drawPrimitives(commands->pipeline->primitive, NS::UInteger{0}, vertexCount,
                                     instanceCount);
    commands->encoderHasWork = true;
}

//======================================================================================================================
void drawIndexed(CommandBuffer* commands, GpuAddress vertexRoot, GpuAddress pixelRoot,
                 GpuAddress indices, IndexKind indexKind, uint32_t indexCount,
                 uint32_t instanceCount) {
    LMX_ASSERT(commands != nullptr && commands->renderPassOpen,
               "drawIndexed: no render pass is open");
    LMX_ASSERT(commands->pipeline != nullptr && commands->pipeline->graphics,
               "drawIndexed: no graphics pipeline is set");
    LMX_ASSERT(indexCount > 0 && instanceCount > 0,
               "drawIndexed: indexCount and instanceCount must be non-zero");
    const uint32_t indexSize = indexSizeOf(indexKind);
    LMX_ASSERT(indices % indexSize == 0, "drawIndexed: the index address must be aligned to the "
                                         "index width");

    const AddressResolution range = resolveAddress(commands->device, indices, "drawIndexed");
    LMX_ASSERT(uint64_t{indexCount} * indexSize <= range.remaining,
               "drawIndexed: the index range reads past the end of its allocation");

    if (vertexRoot != kNullAddress) {
        bindAddress(commands, kVertexRootBindIndex, vertexRoot);
    }
    if (pixelRoot != kNullAddress) {
        bindAddress(commands, kPixelRootBindIndex, pixelRoot);
    }
    // Metal wants the bytes remaining at the address rather than the whole buffer's length.
    commands->render->drawIndexedPrimitives(commands->pipeline->primitive, indexCount,
                                            toMTL(indexKind), indices, range.remaining,
                                            instanceCount);
    commands->encoderHasWork = true;
}

//======================================================================================================================
void drawIndirect(CommandBuffer* commands, GpuAddress vertexRoot, GpuAddress pixelRoot,
                  GpuAddress arguments) {
    LMX_ASSERT(commands != nullptr && commands->renderPassOpen,
               "drawIndirect: no render pass is open");
    LMX_ASSERT(commands->pipeline != nullptr && commands->pipeline->graphics,
               "drawIndirect: no graphics pipeline is set");
    LMX_ASSERT(arguments % commands->device->caps.minIndirectAlignment == 0,
               "drawIndirect: the argument address violates the device's indirect alignment");
    const AddressResolution range = resolveAddress(commands->device, arguments, "drawIndirect");
    LMX_ASSERT(range.remaining >= 4 * sizeof(uint32_t),
               "drawIndirect: the arguments read past the end of their allocation");

    if (vertexRoot != kNullAddress) {
        bindAddress(commands, kVertexRootBindIndex, vertexRoot);
    }
    if (pixelRoot != kNullAddress) {
        bindAddress(commands, kPixelRootBindIndex, pixelRoot);
    }
    commands->render->drawPrimitives(commands->pipeline->primitive, arguments);
    commands->encoderHasWork = true;
}

//======================================================================================================================
void drawIndexedIndirect(CommandBuffer* commands, GpuAddress vertexRoot, GpuAddress pixelRoot,
                         GpuAddress indices, IndexKind indexKind, GpuAddress arguments) {
    LMX_ASSERT(commands != nullptr && commands->renderPassOpen,
               "drawIndexedIndirect: no render pass is open");
    LMX_ASSERT(commands->pipeline != nullptr && commands->pipeline->graphics,
               "drawIndexedIndirect: no graphics pipeline is set");
    LMX_ASSERT(indices % indexSizeOf(indexKind) == 0,
               "drawIndexedIndirect: the index address must be aligned to the index width");
    LMX_ASSERT(arguments % commands->device->caps.minIndirectAlignment == 0,
               "drawIndexedIndirect: the argument address violates the device's indirect "
               "alignment");
    const AddressResolution indexRange =
        resolveAddress(commands->device, indices, "drawIndexedIndirect indices");
    const AddressResolution argumentRange =
        resolveAddress(commands->device, arguments, "drawIndexedIndirect arguments");
    LMX_ASSERT(argumentRange.remaining >= 5 * sizeof(uint32_t),
               "drawIndexedIndirect: the arguments read past the end of their allocation");

    if (vertexRoot != kNullAddress) {
        bindAddress(commands, kVertexRootBindIndex, vertexRoot);
    }
    if (pixelRoot != kNullAddress) {
        bindAddress(commands, kPixelRootBindIndex, pixelRoot);
    }
    commands->render->drawIndexedPrimitives(commands->pipeline->primitive, toMTL(indexKind),
                                            indices, indexRange.remaining, arguments);
    commands->encoderHasWork = true;
}

//======================================================================================================================
void dispatch(CommandBuffer* commands, GpuAddress root, uint32_t groupsX, uint32_t groupsY,
              uint32_t groupsZ) {
    LMX_ASSERT(commands != nullptr && commands->recording,
               "dispatch: this command buffer is not recording");
    LMX_ASSERT(!commands->renderPassOpen, "dispatch: a render pass is open");
    LMX_ASSERT(commands->pipeline != nullptr && !commands->pipeline->graphics,
               "dispatch: no compute pipeline is set");
    LMX_ASSERT(groupsX > 0 && groupsY > 0 && groupsZ > 0,
               "dispatch: every threadgroup count must be non-zero");

    MTL4::ComputeCommandEncoder* encoder = ensureComputeEncoder(commands, "dispatch");
    restorePipeline(commands, encoder);
    if (root != kNullAddress) {
        bindAddress(commands, kVertexRootBindIndex, root);
    }
    encoder->dispatchThreadgroups(MTL::Size::Make(groupsX, groupsY, groupsZ),
                                  MTL::Size::Make(kThreadgroupWidth, kThreadgroupHeight, 1));
    commands->encoderHasWork = true;
}

//======================================================================================================================
void dispatchIndirect(CommandBuffer* commands, GpuAddress root, GpuAddress arguments) {
    LMX_ASSERT(commands != nullptr && commands->recording,
               "dispatchIndirect: this command buffer is not recording");
    LMX_ASSERT(!commands->renderPassOpen, "dispatchIndirect: a render pass is open");
    LMX_ASSERT(commands->pipeline != nullptr && !commands->pipeline->graphics,
               "dispatchIndirect: no compute pipeline is set");
    LMX_ASSERT(arguments % commands->device->caps.minIndirectAlignment == 0,
               "dispatchIndirect: the argument address violates the device's indirect alignment");
    const AddressResolution range = resolveAddress(commands->device, arguments, "dispatchIndirect");
    LMX_ASSERT(range.remaining >= 3 * sizeof(uint32_t),
               "dispatchIndirect: the arguments read past the end of their allocation");

    MTL4::ComputeCommandEncoder* encoder = ensureComputeEncoder(commands, "dispatchIndirect");
    restorePipeline(commands, encoder);
    if (root != kNullAddress) {
        bindAddress(commands, kVertexRootBindIndex, root);
    }
    // Only the threadgroup counts come from memory; the shape stays the pinned prototype constant.
    encoder->dispatchThreadgroups(arguments,
                                  MTL::Size::Make(kThreadgroupWidth, kThreadgroupHeight, 1));
    commands->encoderHasWork = true;
}

//======================================================================================================================
CommandBufferStats commandBufferStats(const CommandBuffer* commands) {
    LMX_ASSERT(commands != nullptr, "commandBufferStats: commands must not be null");
    return {.setAddressCalls = commands->stats.setAddressCalls,
            .rootCalls = commands->stats.rootCalls,
            .rootBytes = commands->stats.rootBytes,
            .barrierCalls = commands->stats.barrierCalls};
}

} // namespace lmx::experimental::noapi
