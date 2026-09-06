//----------------------------------------------------------------------------------------------------------------------
/// @file Host.cpp
/// @brief Executes and validates isolated three-slot native Metal 4 submission workloads.
//----------------------------------------------------------------------------------------------------------------------
#include "Host.h"

#include "Core/Assert.h"
#include "VerificationFeedback.h"

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <sstream>
#include <syncstream>

namespace lmx::experimental::submission {
namespace {

using Clock = std::chrono::steady_clock;
constexpr uint64_t kImageBytes = uint64_t{kExtent} * kExtent * 4;
constexpr uint64_t kStorageLimit = 256ULL * 1024 * 1024;
constexpr uint64_t kGuardBytes = 256;
constexpr uint64_t kParamStride = 256;
constexpr uint64_t kTimeoutMs = 60'000;
constexpr uint32_t kSlots = 3;
constexpr MTL::Stages kRenderStages = MTL::StageVertex | MTL::StageFragment;

struct DrawArgs {
    uint32_t vertexCount, instanceCount, firstVertex, firstInstance;
};
static_assert(sizeof(DrawArgs) == 16 && offsetof(DrawArgs, firstVertex) == 8);
static_assert(sizeof(DrawArgs) == sizeof(MTL::DrawPrimitivesIndirectArguments));

struct GuardedBuffer {
    NS::SharedPtr<MTL::Buffer> object;
    uint64_t bytes = 0;
};

struct FrameSlot {
    NS::SharedPtr<MTL4::CommandAllocator> allocator;
    NS::SharedPtr<MTL4::CommandBuffer> commands;
    NS::SharedPtr<MTL4::ArgumentTable> table;
    std::array<GuardedBuffer, 5> buffers;
    NS::SharedPtr<MTL::Texture> color, depth;
    NS::SharedPtr<MTL::Buffer> readback;
    NS::SharedPtr<MTL4::RenderPassDescriptor> pass;
    std::array<uint32_t, kMaxBins + 1> binOffsets{};
    uint64_t completion = 0, allocatorHighWater = 0;
    uint32_t visibleCount = 0, guard = 0, slotIndex = 0, logicalFrame = 0;
    const FrameInput* input = nullptr;
    bool pending = false;
};

// This aggregate is explicitly destroyed under its owner's autorelease pool after retirement.
struct NativeState {
    NS::SharedPtr<MTL::Device> device;
    NS::SharedPtr<MTL4::CommandQueue> queue;
    NS::SharedPtr<MTL4::Compiler> compiler;
    NS::SharedPtr<MTL::ResidencySet> residency;
    NS::SharedPtr<MTL::SharedEvent> event;
    NS::SharedPtr<MTL::Library> sceneLibrary, prepareLibrary;
    NS::SharedPtr<MTL::RenderPipelineState> raster;
    NS::SharedPtr<MTL::ComputePipelineState> prepare;
    NS::SharedPtr<MTL::DepthStencilState> depthState;
    NS::SharedPtr<MTL::Buffer> colors;
    NS::SharedPtr<NS::String> prepareLabel, rasterLabel, readbackLabel;
    std::array<FrameSlot, kSlots> slots;
};

//======================================================================================================================
NS::SharedPtr<NS::String> text(std::string_view value) {
    return NS::TransferPtr(
        NS::String::alloc()->init(std::string(value).c_str(), NS::UTF8StringEncoding));
}

//======================================================================================================================
std::string describe(NS::Error* error) {
    return error && error->localizedDescription()
               ? std::string(error->localizedDescription()->utf8String())
               : "native object creation returned null";
}

//======================================================================================================================
uint64_t nanoseconds(Clock::duration elapsed) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
}

//======================================================================================================================
double milliseconds(Clock::duration elapsed) {
    return std::chrono::duration<double, std::milli>(elapsed).count();
}

//======================================================================================================================
bool enabled(const char* key) {
    const char* value = std::getenv(key);
    return value && *value && std::string_view(value) != "0";
}

//======================================================================================================================
std::byte* data(GuardedBuffer& buffer) {
    return static_cast<std::byte*>(buffer.object->contents()) + kGuardBytes;
}

//======================================================================================================================
MTL::GPUAddress address(const GuardedBuffer& buffer) {
    return buffer.object->gpuAddress() + kGuardBytes;
}

//======================================================================================================================
bool cpuVisible(const Instance& instance, const Params& params) {
    // makeFrame validates the camera and every bound before warmup; only classification is timed.
    for (const auto& plane : params.planes) {
        const double distance = double(plane[0]) * instance.x + double(plane[1]) * instance.y +
                                double(plane[2]) * instance.z + plane[3];
        if (distance < -double(instance.radius) - params.epsilon) {
            return false;
        }
    }
    return true;
}

//======================================================================================================================
Result<NS::SharedPtr<MTL::Library>> loadLibrary(MTL::Device* device,
                                                const std::filesystem::path& base) {
    NS::Error* error = nullptr;
    NS::SharedPtr<MTL::Library> library;
    std::error_code ec;
    const auto binary = base.string() + ".metallib";
    if (std::filesystem::is_regular_file(binary, ec)) {
        auto url = NS::TransferPtr(NS::URL::alloc()->initFileURLWithPath(text(binary).get()));
        library = NS::TransferPtr(device->newLibrary(url.get(), &error));
    } else {
        std::ifstream stream(base.string() + ".metal", std::ios::binary);
        if (!stream) {
            return std::unexpected("Missing compiled Slang artifact: " + base.string());
        }
        std::string source{std::istreambuf_iterator<char>(stream), {}};
        if (stream.bad()) {
            return std::unexpected("Cannot read shader artifact: " + base.string());
        }
        auto options = NS::TransferPtr(MTL::CompileOptions::alloc()->init());
        options->setLanguageVersion(MTL::LanguageVersion4_0);
        library = NS::TransferPtr(device->newLibrary(text(source).get(), options.get(), &error));
    }
    if (!library) {
        return std::unexpected("Load " + base.string() + ": " + describe(error));
    }
    library->setLabel(text("lmx.submission." + base.filename().string()).get());
    return library;
}

class NativeHost {
public:
    ~NativeHost();
    Result<void> initialize(const Case&, Suite, Variant, const RunConfig&);
    Result<FrameSample> submit(uint32_t, const FrameInput&, uint32_t, Clock::time_point* = nullptr);
    Result<void> wait(FrameSlot&);
    Result<void> drain();
    Result<FrameImage> read(FrameSlot&);
    Result<void> beginCapture(const std::filesystem::path&);
    Result<void> captureMetadata(const std::filesystem::path&, uint32_t, uint32_t);
    void endCapture();
    uint64_t allocatedBytes();

    std::unique_ptr<NativeState> state = std::make_unique<NativeState>();
    uint64_t requestedBytes = 0;

private:
    Result<void> buffer(GuardedBuffer&, uint64_t, const std::string&);
    Result<void> pipelines(const std::filesystem::path&);
    void commit(FrameSlot&, std::string_view, Clock::time_point* = nullptr);
    Result<void> retireThrough(uint64_t);
    std::shared_ptr<detail::VerificationFeedback> m_feedback;
    Case m_spec;
    Suite m_suite = Suite::S;
    Variant m_variant = Variant::Direct;
    uint64_t m_sequence = 0, m_resourceAllocated = 0;
    bool m_verify = false, m_capturing = false;
};

//======================================================================================================================
NativeHost::~NativeHost() {
    auto pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
    if (m_feedback && m_sequence) {
        const auto delivered = m_feedback->waitThrough(
            m_sequence, Clock::now() + std::chrono::milliseconds(kTimeoutMs));
        // Feedback runs after a committed workload finishes, including failed workloads. Every
        // submitted callback must arrive before native resources can be released after a failure.
        LMX_ASSERT(m_feedback->deliveredThrough(m_sequence),
                   delivered ? "verification teardown: incomplete callbacks" : delivered.error());
    } else if (state->queue && state->event && m_sequence) {
        // Releasing in-flight storage is not a recoverable teardown operation.
        LMX_ASSERT(state->event->waitUntilSignaledValue(m_sequence, kTimeoutMs),
                   "submission teardown: GPU did not retire submitted storage");
    }
    endCapture();
    if (state->queue && state->residency) {
        state->queue->removeResidencySet(state->residency.get());
    }
    state.reset();
    m_feedback.reset();
}

//======================================================================================================================
Result<void> NativeHost::buffer(GuardedBuffer& output, uint64_t bytes, const std::string& label) {
    output.bytes = std::max<uint64_t>(bytes, 4);
    output.object = NS::TransferPtr(state->device->newBuffer(
        output.bytes + 2 * kGuardBytes,
        MTL::ResourceStorageModeShared | MTL::ResourceHazardTrackingModeUntracked));
    if (!output.object) {
        return std::unexpected("Allocate " + label);
    }
    output.object->setLabel(text(label).get());
    std::memset(output.object->contents(), 0, output.object->length());
    state->residency->addAllocation(output.object.get());
    requestedBytes += output.object->length();
    m_resourceAllocated += output.object->allocatedSize();
    return {};
}

//======================================================================================================================
Result<void> NativeHost::pipelines(const std::filesystem::path& directory) {
    auto scene = loadLibrary(state->device.get(), directory / "Scene");
    auto prepare = loadLibrary(state->device.get(), directory / "Prepare");
    if (!scene || !prepare) {
        return std::unexpected(!scene ? scene.error() : prepare.error());
    }
    state->sceneLibrary = std::move(*scene);
    state->prepareLibrary = std::move(*prepare);
    auto vertex = NS::TransferPtr(MTL4::LibraryFunctionDescriptor::alloc()->init());
    auto fragment = NS::TransferPtr(MTL4::LibraryFunctionDescriptor::alloc()->init());
    auto kernel = NS::TransferPtr(MTL4::LibraryFunctionDescriptor::alloc()->init());
    const std::array libraries{state->sceneLibrary.get(), state->sceneLibrary.get(),
                               state->prepareLibrary.get()};
    const std::array functions{vertex.get(), fragment.get(), kernel.get()};
    const std::array names{"vertexMain", "fragmentMain", "computeMain"};
    const std::array types{MTL::FunctionTypeVertex, MTL::FunctionTypeFragment,
                           MTL::FunctionTypeKernel};
    for (size_t i = 0; i < names.size(); ++i) {
        auto function = NS::TransferPtr(libraries[i]->newFunction(text(names[i]).get()));
        if (!function || function->functionType() != types[i]) {
            return std::unexpected("Missing or wrong-stage shader function " +
                                   std::string(names[i]));
        }
        functions[i]->setLibrary(libraries[i]);
        functions[i]->setName(text(names[i]).get());
    }
    auto raster = NS::TransferPtr(MTL4::RenderPipelineDescriptor::alloc()->init());
    raster->setLabel(text("lmx.submission.raster.pipeline").get());
    raster->setVertexFunctionDescriptor(vertex.get());
    raster->setFragmentFunctionDescriptor(fragment.get());
    raster->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
    raster->setRasterSampleCount(1);
    NS::Error* error = nullptr;
    state->raster =
        NS::TransferPtr(state->compiler->newRenderPipelineState(raster.get(), nullptr, &error));
    if (!state->raster) {
        return std::unexpected("Create raster pipeline: " + describe(error));
    }
    auto compute = NS::TransferPtr(MTL4::ComputePipelineDescriptor::alloc()->init());
    compute->setLabel(text("lmx.submission.prepare.pipeline").get());
    compute->setComputeFunctionDescriptor(kernel.get());
    error = nullptr;
    state->prepare =
        NS::TransferPtr(state->compiler->newComputePipelineState(compute.get(), nullptr, &error));
    if (!state->prepare || state->prepare->maxTotalThreadsPerThreadgroup() < 64) {
        return std::unexpected("Create 64-thread preparation pipeline: " + describe(error));
    }
    auto depth = NS::TransferPtr(MTL::DepthStencilDescriptor::alloc()->init());
    depth->setLabel(text("lmx.submission.reversedZ").get());
    depth->setDepthCompareFunction(MTL::CompareFunctionGreater);
    depth->setDepthWriteEnabled(true);
    state->depthState = NS::TransferPtr(state->device->newDepthStencilState(depth.get()));
    if (!state->depthState) {
        return std::unexpected("Create reversed-Z depth state");
    }
    return {};
}

//======================================================================================================================
Result<void> NativeHost::initialize(const Case& spec, Suite suite, Variant variant,
                                    const RunConfig& config) {
    auto pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
    if (auto valid = validateCase(spec); !valid) {
        return std::unexpected(valid.error());
    }
    if ((suite != Suite::S && suite != Suite::E) || name(variant) == "unknown") {
        return std::unexpected("Unknown native suite or variant");
    }
    if (variant == Variant::GpuIcb) {
        return std::unexpected(
            "GPU ICB unavailable: pinned Slang rejects render_command in "
            "Shaders/Probe.slang (E30015); generation/execution remains unverified");
    }
    m_spec = spec;
    m_suite = suite;
    m_variant = variant;
    m_verify = config.verify;
    state->prepareLabel = text("lmx.submission.prepare.arguments");
    state->rasterLabel = text("lmx.submission.raster");
    state->readbackLabel = text("lmx.submission.validation.readback");
    const uint64_t slotBytes =
        2 * kImageBytes + (m_verify ? kImageBytes : 0) + spec.bins * kParamStride +
        uint64_t{spec.count} * (sizeof(Instance) + 24) + 10 * kGuardBytes + 64;
    const uint64_t feedbackCapacity =
        m_verify ? std::max<uint64_t>(2, 2 * (uint64_t{config.warmup} + config.frames)) : 0;
    if (slotBytes * kSlots + spec.bins * 16 + feedbackCapacity > kStorageLimit) {
        return std::unexpected("Native three-slot requested storage exceeds 256 MiB ceiling");
    }
    if (m_verify) {
        m_feedback = std::make_shared<detail::VerificationFeedback>(feedbackCapacity);
        requestedBytes += m_feedback->requestedBytes();
    }
    state->device = NS::TransferPtr(MTL::CreateSystemDefaultDevice());
    if (!state->device || !state->device->supportsFamily(MTL::GPUFamilyMetal4)) {
        return std::unexpected("A Metal 4 Apple Silicon device is required");
    }
    NS::Error* error = nullptr;
    auto queue = NS::TransferPtr(MTL4::CommandQueueDescriptor::alloc()->init());
    queue->setLabel(text("lmx.submission.queue").get());
    state->queue = NS::TransferPtr(state->device->newMTL4CommandQueue(queue.get(), &error));
    if (!state->queue) {
        return std::unexpected("Create queue: " + describe(error));
    }
    auto compiler = NS::TransferPtr(MTL4::CompilerDescriptor::alloc()->init());
    compiler->setLabel(text("lmx.submission.compiler").get());
    error = nullptr;
    state->compiler = NS::TransferPtr(state->device->newCompiler(compiler.get(), &error));
    if (!state->compiler) {
        return std::unexpected("Create compiler: " + describe(error));
    }
    auto residency = NS::TransferPtr(MTL::ResidencySetDescriptor::alloc()->init());
    residency->setLabel(text("lmx.submission.residency").get());
    error = nullptr;
    state->residency = NS::TransferPtr(state->device->newResidencySet(residency.get(), &error));
    if (!state->residency) {
        return std::unexpected("Create residency set: " + describe(error));
    }
    state->event = NS::TransferPtr(state->device->newSharedEvent());
    if (!state->event) {
        return std::unexpected("Create retirement event");
    }
    state->event->setLabel(text("lmx.submission.retirement").get());
    state->event->setSignaledValue(0);
    if (auto result = pipelines(config.shaderDirectory); !result) {
        return result;
    }
    state->colors =
        NS::TransferPtr(state->device->newBuffer(spec.bins * 16, MTL::ResourceStorageModeShared));
    if (!state->colors) {
        return std::unexpected("Allocate material colors");
    }
    state->colors->setLabel(text("lmx.submission.colors").get());
    auto* colors = static_cast<std::array<float, 4>*>(state->colors->contents());
    for (uint32_t bin = 0; bin < spec.bins; ++bin) {
        colors[bin] = {(bin * 37 % 251 + 1) / 252.f, (bin * 73 % 251 + 1) / 252.f,
                       (bin * 109 % 251 + 1) / 252.f, 1.f};
    }
    requestedBytes += spec.bins * 16;
    m_resourceAllocated += state->colors->allocatedSize();
    state->residency->addAllocation(state->colors.get());
    const std::array<uint64_t, 5> sizes{
        spec.bins * kParamStride, uint64_t{spec.count} * sizeof(Instance), uint64_t{spec.count} * 4,
        uint64_t{spec.count} * sizeof(DrawArgs), uint64_t{spec.count} * 4};
    const std::array bufferNames{"params", "instances", "bitmap", "arguments", "visibleIds"};
    for (uint32_t i = 0; i < kSlots; ++i) {
        auto& slot = state->slots[i];
        slot.slotIndex = i;
        const auto prefix = "lmx.submission.slot" + std::to_string(i) + ".";
        auto allocator = NS::TransferPtr(MTL4::CommandAllocatorDescriptor::alloc()->init());
        allocator->setLabel(text(prefix + "allocator").get());
        error = nullptr;
        slot.allocator =
            NS::TransferPtr(state->device->newCommandAllocator(allocator.get(), &error));
        if (!slot.allocator) {
            return std::unexpected("Create allocator: " + describe(error));
        }
        slot.commands = NS::TransferPtr(state->device->newCommandBuffer());
        if (!slot.commands) {
            return std::unexpected("Create command buffer");
        }
        slot.commands->setLabel(text(prefix + "commands").get());
        auto table = NS::TransferPtr(MTL4::ArgumentTableDescriptor::alloc()->init());
        table->setLabel(text(prefix + "bindings").get());
        table->setMaxBufferBindCount(6);
        table->setInitializeBindings(true);
        error = nullptr;
        slot.table = NS::TransferPtr(state->device->newArgumentTable(table.get(), &error));
        if (!slot.table) {
            return std::unexpected("Create argument table: " + describe(error));
        }
        for (size_t binding = 0; binding < sizes.size(); ++binding) {
            if (auto result =
                    buffer(slot.buffers[binding], sizes[binding], prefix + bufferNames[binding]);
                !result) {
                return result;
            }
            slot.table->setAddress(address(slot.buffers[binding]), binding);
        }
        slot.table->setAddress(state->colors->gpuAddress(), 5);
        for (bool isDepth : {false, true}) {
            auto desc = NS::TransferPtr(MTL::TextureDescriptor::alloc()->init());
            desc->setTextureType(MTL::TextureType2D);
            desc->setWidth(kExtent);
            desc->setHeight(kExtent);
            desc->setPixelFormat(isDepth ? MTL::PixelFormatDepth32Float
                                         : MTL::PixelFormatRGBA8Unorm);
            desc->setStorageMode(MTL::StorageModePrivate);
            desc->setHazardTrackingMode(MTL::HazardTrackingModeUntracked);
            desc->setUsage(MTL::TextureUsageRenderTarget);
            auto& texture = isDepth ? slot.depth : slot.color;
            texture = NS::TransferPtr(state->device->newTexture(desc.get()));
            if (!texture) {
                return std::unexpected("Allocate slot attachment");
            }
            texture->setLabel(text(prefix + (isDepth ? "depth" : "color")).get());
            state->residency->addAllocation(texture.get());
            requestedBytes += kImageBytes;
            m_resourceAllocated += texture->allocatedSize();
        }
        if (m_verify) {
            slot.readback = NS::TransferPtr(
                state->device->newBuffer(kImageBytes, MTL::ResourceStorageModeShared));
            if (!slot.readback) {
                return std::unexpected("Allocate validation-only readback");
            }
            slot.readback->setLabel(text(prefix + "validationReadback").get());
            state->residency->addAllocation(slot.readback.get());
            requestedBytes += kImageBytes;
            m_resourceAllocated += slot.readback->allocatedSize();
        }
        slot.pass = NS::TransferPtr(MTL4::RenderPassDescriptor::alloc()->init());
        auto* color = slot.pass->colorAttachments()->object(0);
        color->setTexture(slot.color.get());
        color->setLoadAction(MTL::LoadActionClear);
        color->setStoreAction(MTL::StoreActionStore);
        color->setClearColor(MTL::ClearColor(0, 0, 0, 1));
        auto* depth = slot.pass->depthAttachment();
        depth->setTexture(slot.depth.get());
        depth->setLoadAction(MTL::LoadActionClear);
        depth->setStoreAction(MTL::StoreActionDontCare);
        depth->setClearDepth(0);
        slot.pass->setDefaultRasterSampleCount(1);
    }
    state->residency->commit();
    state->queue->addResidencySet(state->residency.get());
    return {};
}

//======================================================================================================================
Result<void> NativeHost::wait(FrameSlot& slot) {
    return retireThrough(slot.completion);
}

//======================================================================================================================
Result<void> NativeHost::drain() {
    return retireThrough(m_sequence);
}

//======================================================================================================================
Result<void> NativeHost::retireThrough(uint64_t sequence) {
    if (!sequence) {
        return {};
    }
    uint64_t eventTimeout = kTimeoutMs;
    if (m_feedback) {
        const auto deadline = Clock::now() + std::chrono::milliseconds(kTimeoutMs);
        if (auto delivered = m_feedback->waitThrough(sequence, deadline); !delivered) {
            return delivered;
        }
        // A callback's successful delivery does not replace the event's normal slot-reuse proof.
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        eventTimeout = static_cast<uint64_t>(std::max<int64_t>(remaining, 1));
    }
    if (!state->event->waitUntilSignaledValue(sequence, eventTimeout)) {
        return std::unexpected("Timed out waiting for exact submission completion " +
                               std::to_string(sequence));
    }
    return {};
}

//======================================================================================================================
void NativeHost::commit(FrameSlot& slot, std::string_view kind, Clock::time_point* firstCommit) {
    slot.commands->endCommandBuffer();
    const MTL4::CommandBuffer* commands[]{slot.commands.get()};
    NS::SharedPtr<MTL4::CommitOptions> options;
    if (m_feedback) {
        options = NS::TransferPtr(MTL4::CommitOptions::alloc()->init());
        LMX_ASSERT(options, "verification commit options allocation failed");
        const uint64_t sequence = m_feedback->recordSubmission();
        LMX_ASSERT(sequence == m_sequence + 1, "verification submission sequence diverged");
        const std::string identity =
            "submission GPU commit " + std::to_string(sequence) + " case=" + m_spec.id +
            " suite=" + name(m_suite) + " mode=" + name(m_variant) +
            " slot=" + std::to_string(slot.slotIndex) +
            " frame=" + std::to_string(slot.logicalFrame) + " kind=" + std::string(kind);
        options->addFeedbackHandler(MTL4::CommitFeedbackHandlerFunction{
            [ledger = m_feedback, sequence, identity](MTL4::CommitFeedback* feedback) {
                std::string failure;
                {
                    auto pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
                    if (auto* error = feedback->error()) {
                        const char* domain =
                            error->domain() ? error->domain()->utf8String() : nullptr;
                        failure = identity + " failed: domain=" + (domain ? domain : "unknown") +
                                  " code=" + std::to_string(error->code()) + " " + describe(error);
                        if (auto* reason = error->localizedFailureReason();
                            reason && reason->utf8String()) {
                            failure += "; " + std::string(reason->utf8String());
                        }
                    }
                }
                if (!failure.empty()) {
                    std::fprintf(stderr, "%s\n", failure.c_str());
                }
                // No native objects, slot references or host pointer escape the callback pool.
                ledger->complete(sequence, std::move(failure));
            }});
    }
    if (firstCommit) {
        *firstCommit = Clock::now();
    }
    if (options) {
        state->queue->commit(commands, 1, options.get());
    } else {
        state->queue->commit(commands, 1);
    }
    slot.completion = ++m_sequence;
    state->queue->signalEvent(state->event.get(), slot.completion);
}

//======================================================================================================================
Result<FrameSample> NativeHost::submit(uint32_t slotIndex, const FrameInput& input,
                                       uint32_t logicalFrame, Clock::time_point* firstCommit) {
    auto pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
    auto& slot = state->slots[slotIndex];
    LMX_ASSERT(!slot.pending, "retired validation must be consumed before slot overwrite");
    FrameSample sample;
    sample.frame = logicalFrame;
    const auto waitStart = Clock::now();
    if (auto result = wait(slot); !result) {
        return std::unexpected(result.error());
    }
    const auto start = Clock::now();
    sample.waitNs = nanoseconds(start - waitStart);
    LMX_ASSERT(state->event->signaledValue() >= slot.completion,
               "submission slot allocator and mutable buffers are still in flight");
    slot.allocator->reset();
    slot.commands->beginCommandBuffer(slot.allocator.get());
    slot.input = &input;
    slot.logicalFrame = logicalFrame;
    if (m_verify) {
        // Guards change on every reuse; scored shaders remain debug=0 with no diagnostic writes.
        slot.guard = 0xC04D0000u ^ static_cast<uint32_t>(m_sequence + 1);
        for (auto& buffer : slot.buffers) {
            auto* head = static_cast<uint32_t*>(buffer.object->contents());
            auto* tail = reinterpret_cast<uint32_t*>(data(buffer) + buffer.bytes);
            std::fill_n(head, kGuardBytes / 4, slot.guard);
            std::fill_n(tail, kGuardBytes / 4, slot.guard);
        }
    }
    const uint64_t instanceBytes = input.instances.size() * sizeof(Instance);
    if (instanceBytes) {
        std::memcpy(data(slot.buffers[1]), input.instances.data(), instanceBytes);
    }
    sample.copiedBytes += instanceBytes;
    const bool gpu = m_variant == Variant::GpuArgs;
    const bool batched = m_variant == Variant::Batched;
    if (m_suite == Suite::S && gpu && m_spec.count) {
        std::memcpy(data(slot.buffers[2]), input.bitmap.data(), uint64_t{m_spec.count} * 4);
        sample.copiedBytes += uint64_t{m_spec.count} * 4;
    }
    // E's GPU path never reads or uploads the oracle bitmap, IDs, count, or bin offsets.
    auto* ids = reinterpret_cast<uint32_t*>(data(slot.buffers[4]));
    auto* arguments = reinterpret_cast<DrawArgs*>(data(slot.buffers[3]));
    slot.visibleCount = 0;
    slot.binOffsets.fill(0);
    if (!gpu) {
        for (uint32_t id = 0; id < m_spec.count; ++id) {
            const bool visible = m_suite == Suite::S
                                     ? input.bitmap[id] != 0
                                     : cpuVisible(input.instances[id], input.params);
            if (visible) {
                ids[slot.visibleCount++] = id;
                ++slot.binOffsets[input.instances[id].bin + 1];
            }
        }
        for (uint32_t bin = 0; bin < m_spec.bins; ++bin) {
            slot.binOffsets[bin + 1] += slot.binOffsets[bin];
        }
        sample.copiedBytes += uint64_t{slot.visibleCount} * 4;
        if (m_variant == Variant::CpuIndirect) {
            for (uint32_t i = 0; i < m_spec.count; ++i) {
                arguments[i] = i < slot.visibleCount ? DrawArgs{3 * m_spec.triangles, 1,
                                                                ids[i] * 3 * m_spec.triangles, 0}
                                                     : DrawArgs{0, 0, 0, 0};
            }
            sample.copiedBytes += uint64_t{m_spec.count} * sizeof(DrawArgs);
        }
    }
    for (uint32_t bin = 0; bin < m_spec.bins; ++bin) {
        Params params = input.params;
        params.suite = m_suite == Suite::S ? 0 : 1;
        params.batched = batched ? 1 : 0;
        params.binOffset = batched ? slot.binOffsets[bin] : 0;
        params.debug = 0;
        std::memcpy(data(slot.buffers[0]) + bin * kParamStride, &params, sizeof(params));
        sample.copiedBytes += sizeof(params);
    }
    slot.table->setAddress(address(slot.buffers[0]), 0);
    if (gpu && m_spec.count) {
        auto* encoder = slot.commands->computeCommandEncoder();
        LMX_ASSERT(encoder, "create preparation encoder");
        encoder->setLabel(state->prepareLabel.get());
        encoder->setComputePipelineState(state->prepare.get());
        encoder->setArgumentTable(slot.table.get());
        encoder->dispatchThreadgroups(MTL::Size::Make((m_spec.count + 63) / 64, 1, 1),
                                      MTL::Size::Make(64, 1, 1));
        encoder->endEncoding();
    }
    auto* encoder = slot.commands->renderCommandEncoder(slot.pass.get());
    LMX_ASSERT(encoder, "create raster encoder");
    encoder->setLabel(state->rasterLabel.get());
    if (gpu && m_spec.count) {
        encoder->barrierAfterQueueStages(MTL::StageDispatch, kRenderStages,
                                         MTL4::VisibilityOptionDevice);
    }
    encoder->setRenderPipelineState(state->raster.get());
    encoder->setDepthStencilState(state->depthState.get());
    encoder->setCullMode(MTL::CullModeNone);
    encoder->setViewport(MTL::Viewport{0, 0, double(kExtent), double(kExtent), 0, 1});
    const uint32_t vertices = 3 * m_spec.triangles;
    for (uint32_t bin = 0; bin < m_spec.bins; ++bin) {
        slot.table->setAddress(address(slot.buffers[0]) + bin * kParamStride, 0);
        encoder->setArgumentTable(slot.table.get(),
                                  MTL::RenderStageVertex | MTL::RenderStageFragment);
        const uint32_t begin =
            gpu ? uint64_t{bin} * m_spec.count / m_spec.bins : slot.binOffsets[bin];
        const uint32_t end =
            gpu ? uint64_t{bin + 1} * m_spec.count / m_spec.bins : slot.binOffsets[bin + 1];
        if (batched) {
            if (end != begin) {
                encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, 0, vertices, end - begin, 0);
                ++sample.drawCalls;
            }
        } else {
            for (uint32_t i = begin; i < end; ++i) {
                if (m_variant == Variant::Direct) {
                    encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, ids[i] * vertices,
                                            vertices);
                } else {
                    encoder->drawPrimitives(MTL::PrimitiveTypeTriangle,
                                            address(slot.buffers[3]) +
                                                uint64_t{i} * sizeof(DrawArgs));
                }
                ++sample.drawCalls;
            }
        }
    }
    encoder->endEncoding();
    commit(slot, "raster", firstCommit);
    sample.cpuWorkNs = nanoseconds(Clock::now() - start);
    slot.allocatorHighWater = std::max(slot.allocatorHighWater, slot.allocator->allocatedSize());
    slot.pending = m_verify;
    return sample;
}

//======================================================================================================================
Result<FrameImage> NativeHost::read(FrameSlot& slot) {
    auto pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
    LMX_ASSERT(m_verify && slot.pending, "read requires a submitted validation frame");
    if (auto result = wait(slot); !result) {
        return std::unexpected(result.error());
    }
    FrameImage image;
    image.visibleIds.reserve(m_spec.count);
    for (auto& buffer : slot.buffers) {
        const auto* head = static_cast<const uint32_t*>(buffer.object->contents());
        const auto* tail = reinterpret_cast<const uint32_t*>(data(buffer) + buffer.bytes);
        for (uint32_t i = 0; i < kGuardBytes / 4; ++i) {
            if (head[i] != slot.guard || tail[i] != slot.guard) {
                return std::unexpected("Retired slot canary changed at completion " +
                                       std::to_string(slot.completion));
            }
        }
    }
    const auto* args = reinterpret_cast<const DrawArgs*>(data(slot.buffers[3]));
    const auto* ids = reinterpret_cast<const uint32_t*>(data(slot.buffers[4]));
    if (m_variant == Variant::GpuArgs) {
        for (uint32_t id = 0; id < m_spec.count; ++id) {
            const auto& arg = args[id];
            if (arg.vertexCount != 3 * m_spec.triangles || arg.instanceCount > 1 ||
                arg.firstVertex != id * 3 * m_spec.triangles || arg.firstInstance != 0) {
                return std::unexpected("Invalid GPU argument readback at object " +
                                       std::to_string(id));
            }
            if (arg.instanceCount) {
                image.visibleIds.push_back(id);
            }
        }
    } else {
        image.visibleIds.assign(ids, ids + slot.visibleCount);
        if (m_variant == Variant::CpuIndirect) {
            for (uint32_t i = 0; i < m_spec.count; ++i) {
                const DrawArgs expected =
                    i < slot.visibleCount
                        ? DrawArgs{3 * m_spec.triangles, 1, ids[i] * 3 * m_spec.triangles, 0}
                        : DrawArgs{0, 0, 0, 0};
                if (std::memcmp(&args[i], &expected, sizeof(expected))) {
                    return std::unexpected("CPU indirect record changed after retirement");
                }
            }
        }
    }
    if (image.visibleIds != slot.input->visibleIds) {
        return std::unexpected("Retired visible ID set/count disagrees with independent oracle");
    }
    // Only correctness replay submits this copy, after the scored artifact has fully retired.
    slot.allocator->reset();
    slot.commands->beginCommandBuffer(slot.allocator.get());
    auto* copy = slot.commands->computeCommandEncoder();
    LMX_ASSERT(copy, "create validation readback encoder");
    copy->setLabel(state->readbackLabel.get());
    copy->barrierAfterQueueStages(kRenderStages, MTL::StageBlit, MTL4::VisibilityOptionDevice);
    copy->copyFromTexture(slot.color.get(), 0, 0, MTL::Origin::Make(0, 0, 0),
                          MTL::Size::Make(kExtent, kExtent, 1), slot.readback.get(), 0,
                          uint64_t{kExtent} * 4, kImageBytes);
    copy->endEncoding();
    commit(slot, "readback");
    if (auto result = wait(slot); !result) {
        return std::unexpected(result.error());
    }
    const auto* pixels = static_cast<const uint8_t*>(slot.readback->contents());
    image.rgba.assign(pixels, pixels + kImageBytes);
    slot.pending = false;
    return image;
}

//======================================================================================================================
uint64_t NativeHost::allocatedBytes() {
    uint64_t result = m_resourceAllocated;
    for (auto& slot : state->slots) {
        slot.allocatorHighWater =
            std::max(slot.allocatorHighWater, slot.allocator->allocatedSize());
        result += slot.allocatorHighWater;
    }
    return result;
}

//======================================================================================================================
Result<void> NativeHost::beginCapture(const std::filesystem::path& path) {
    auto pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
    std::error_code ec;
    if (!path.is_absolute() || path.extension() != ".gputrace" ||
        std::filesystem::exists(path, ec) || ec) {
        return std::unexpected("Capture requires a new absolute .gputrace path");
    }
    auto* manager = MTL::CaptureManager::sharedCaptureManager();
    if (!manager || manager->isCapturing() ||
        !manager->supportsDestination(MTL::CaptureDestinationGPUTraceDocument)) {
        return std::unexpected(
            "Capture unavailable or already active; launch with MTL_CAPTURE_ENABLED=1");
    }
    auto url = NS::TransferPtr(NS::URL::alloc()->initFileURLWithPath(text(path.string()).get()));
    auto desc = NS::TransferPtr(MTL::CaptureDescriptor::alloc()->init());
    desc->setCaptureObject(state->device.get());
    desc->setDestination(MTL::CaptureDestinationGPUTraceDocument);
    desc->setOutputURL(url.get());
    NS::Error* error = nullptr;
    if (!manager->startCapture(desc.get(), &error)) {
        return std::unexpected("Start capture: " + describe(error));
    }
    m_capturing = true;
    return {};
}

//======================================================================================================================
void NativeHost::endCapture() {
    if (m_capturing) {
        MTL::CaptureManager::sharedCaptureManager()->stopCapture();
        m_capturing = false;
    }
}

//======================================================================================================================
Result<void> NativeHost::captureMetadata(const std::filesystem::path& path, uint32_t slotIndex,
                                         uint32_t logicalFrame) {
    const auto sidecar = path.string() + ".capture.json";
    std::error_code ec;
    if (std::filesystem::exists(sidecar, ec) || ec) {
        return std::unexpected("Capture metadata path already exists or cannot be inspected");
    }
    const auto& slot = state->slots[slotIndex];
    std::ofstream out(sidecar, std::ios::binary);
    if (!out) {
        return std::unexpected("Cannot open capture metadata " + sidecar);
    }
    out << "{\"schemaVersion\":1,\"format\":\"lmx.submission.capture\",\"slot\":" << slotIndex
        << ",\"logicalFrame\":" << logicalFrame << ",\"completionValue\":" << slot.completion
        << ",\"suite\":\"" << name(m_suite) << "\",\"variant\":\"" << name(m_variant)
        << "\",\"candidateCount\":" << m_spec.count << ",\"triangles\":" << m_spec.triangles
        << ",\"bins\":" << m_spec.bins << ",\"paramsStride\":" << kParamStride
        << ",\"instanceStride\":" << sizeof(Instance) << ",\"argumentStride\":" << sizeof(DrawArgs)
        << ",\"argumentFields\":[\"vertexCount\",\"instanceCount\",\"firstVertex\","
           "\"firstInstance\"]"
        << ",\"buffers\":[";
    for (size_t binding = 0; binding < slot.buffers.size(); ++binding) {
        const auto& buffer = slot.buffers[binding];
        if (binding) {
            out << ',';
        }
        out << "{\"binding\":" << binding << ",\"label\":\"" << buffer.object->label()->utf8String()
            << "\",\"dataOffset\":" << kGuardBytes << ",\"dataBytes\":" << buffer.bytes
            << ",\"requestedBytes\":" << buffer.object->length()
            << ",\"allocatedBytes\":" << buffer.object->allocatedSize()
            << ",\"gpuAddress\":" << buffer.object->gpuAddress() << '}';
    }
    out << ",{\"binding\":5,\"label\":\"lmx.submission.colors\",\"dataOffset\":0,\"dataBytes\":"
        << state->colors->length() << "}],\"textures\":[";
    for (bool isDepth : {false, true}) {
        const auto& texture = isDepth ? slot.depth : slot.color;
        if (isDepth) {
            out << ',';
        }
        out << "{\"label\":\"" << texture->label()->utf8String() << "\",\"width\":" << kExtent
            << ",\"height\":" << kExtent << ",\"format\":\""
            << (isDepth ? "D32Float" : "RGBA8Unorm")
            << "\",\"allocatedBytes\":" << texture->allocatedSize() << '}';
    }
    out << "],\"clearColor\":[0,0,0,1],\"clearDepth\":0,\"residentBytes\":null}";
    out.flush();
    if (!out) {
        return std::unexpected("Incomplete capture metadata write " + sidecar);
    }
    return {};
}

//======================================================================================================================
Result<void> compare(const FrameImage& reference, const FrameImage& candidate) {
    if (reference.visibleIds != candidate.visibleIds || reference.rgba.size() != kImageBytes ||
        candidate.rgba.size() != kImageBytes) {
        return std::unexpected("Native image dimensions or visible IDs disagree");
    }
    for (size_t pixel = 0; pixel < kImageBytes; pixel += 4) {
        const bool a =
            reference.rgba[pixel] || reference.rgba[pixel + 1] || reference.rgba[pixel + 2];
        const bool b =
            candidate.rgba[pixel] || candidate.rgba[pixel + 1] || candidate.rgba[pixel + 2];
        if (a != b) {
            return std::unexpected("Native image coverage mismatch at pixel " +
                                   std::to_string(pixel / 4));
        }
        for (size_t channel = 0; channel < 4; ++channel) {
            if (std::abs(int(reference.rgba[pixel + channel]) -
                         int(candidate.rgba[pixel + channel])) > 1) {
                return std::unexpected("Native image differs by more than 1/255");
            }
        }
    }
    return {};
}

} // namespace

//======================================================================================================================
Result<FrameImage> renderNativeFrame(const Case& spec, Suite suite, Variant variant,
                                     uint32_t logicalFrame, const RunConfig& config) {
    auto pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
    auto input = makeFrame(spec, logicalFrame);
    if (!input) {
        return std::unexpected(input.error());
    }
    auto validation = config;
    validation.verify = true;
    NativeHost host;
    if (auto result = host.initialize(spec, suite, variant, validation); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = host.submit(0, *input, logicalFrame); !result) {
        return std::unexpected(result.error());
    }
    return host.read(host.state->slots[0]);
}

//======================================================================================================================
Result<RunResult> runNative(const Case& spec, Suite suite, Variant variant, Lane lane,
                            const RunConfig& config) {
    auto pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
    if (!config.frames || name(lane) == "unknown") {
        return std::unexpected("Native run requires positive frame count and a valid lane");
    }
    if (lane != Lane::Headline) {
        return std::unexpected(
            "unavailable: gpu-span and stages require verified timestamp "
            "boundaries; this host implements only the marker-free headline lane");
    }
    if (!config.verify && (enabled("MTL_DEBUG_LAYER") || enabled("MTL_SHADER_VALIDATION") ||
                           enabled("MTL_CAPTURE_ENABLED") || !config.capturePath.empty())) {
        return std::unexpected(
            "Scored native runs refuse validation/capture; use verification replay");
    }
    std::array<FrameInput, kPhaseCount> inputs;
    for (uint32_t phase = 0; phase < kPhaseCount; ++phase) {
        auto input = makeFrame(spec, phase * kFramesPerPhase);
        if (!input) {
            return std::unexpected(input.error());
        }
        inputs[phase] = std::move(*input);
    }
    std::array<FrameImage, kPhaseCount> references;
    if (config.verify) {
        auto referenceConfig = config;
        referenceConfig.capturePath.clear();
        // Reference native hosts are destroyed before the candidate's resource set is created.
        for (uint32_t phase = 0; phase < kPhaseCount; ++phase) {
            auto image = renderNativeFrame(spec, suite, Variant::Direct, phase * kFramesPerPhase,
                                           referenceConfig);
            if (!image) {
                return std::unexpected(image.error());
            }
            references[phase] = std::move(*image);
        }
    }
    RunResult result;
    result.samples.reserve(config.frames);
    NativeHost host;
    const auto setupStart = Clock::now();
    if (auto initialized = host.initialize(spec, suite, variant, config); !initialized) {
        return std::unexpected(initialized.error());
    }
    result.setupMs = milliseconds(Clock::now() - setupStart);
    result.device = host.state->device->name()->utf8String();
    result.requestedBytes = host.requestedBytes;
    // Requested CPU inputs, scratch, results and validation images are not GPU allocated bytes.
    for (const auto& input : inputs) {
        result.requestedBytes +=
            input.instances.capacity() * sizeof(Instance) +
            (input.bitmap.capacity() + input.visibleIds.capacity() + input.binOffsets.capacity()) *
                4;
    }
    for (const auto& image : references) {
        result.requestedBytes += image.rgba.capacity() + image.visibleIds.capacity() * 4;
    }
    result.requestedBytes += result.samples.capacity() * sizeof(FrameSample) + sizeof(NativeState);
    if (config.verify) {
        // One retired readback image/ID vector is live beside the eight retained phase oracles.
        result.requestedBytes += kImageBytes + uint64_t{spec.count} * sizeof(uint32_t);
    }
    if (result.requestedBytes > kStorageLimit) {
        return std::unexpected("Complete variant requested storage exceeds 256 MiB ceiling");
    }
    result.gpuSpanStatus = "unavailable: timestamp workload-boundary ordering is unverified; "
                           "no markers are emitted in any lane, including stages";
    auto retire = [&](FrameSlot& slot) -> Result<void> {
        if (!slot.pending) {
            return {};
        }
        const auto phase = static_cast<size_t>(slot.input - inputs.data());
        auto image = host.read(slot);
        if (!image) {
            return std::unexpected(image.error());
        }
        return compare(references[phase], *image);
    };
    for (uint32_t frame = 0; frame < config.warmup; ++frame) {
        auto& slot = host.state->slots[frame % kSlots];
        if (auto retired = retire(slot); !retired) {
            return std::unexpected(retired.error());
        }
        // Warm every phase and bin occupancy before freezing allocator high-water marks.
        auto sample = host.submit(frame % kSlots, inputs[frame % kPhaseCount], frame);
        if (!sample) {
            return std::unexpected(sample.error());
        }
    }
    if (auto drained = host.drain(); !drained) {
        return std::unexpected(drained.error());
    }
    for (auto& slot : host.state->slots) {
        if (auto retired = retire(slot); !retired) {
            return std::unexpected(retired.error());
        }
    }
    const uint64_t warmedAllocated = host.allocatedBytes();
    if (!config.capturePath.empty()) {
        if (auto capture = host.beginCapture(config.capturePath); !capture) {
            return std::unexpected(capture.error());
        }
    }
    Clock::time_point firstCommit;
    for (uint32_t frame = 0; frame < config.frames; ++frame) {
        auto& slot = host.state->slots[frame % kSlots];
        if (auto retired = retire(slot); !retired) {
            return std::unexpected(retired.error());
        }
        auto sample = host.submit(frame % kSlots, inputs[(frame / kFramesPerPhase) % kPhaseCount],
                                  frame, frame == 0 ? &firstCommit : nullptr);
        if (!sample) {
            return std::unexpected(sample.error());
        }
        result.samples.push_back(*sample);
        if (frame == 0 && !config.capturePath.empty()) {
            if (auto drained = host.drain(); !drained) {
                return std::unexpected(drained.error());
            }
            host.endCapture();
            if (auto metadata = host.captureMetadata(config.capturePath, frame % kSlots, frame);
                !metadata) {
                return std::unexpected(metadata.error());
            }
        }
    }
    const auto drainStart = Clock::now();
    if (auto drained = host.drain(); !drained) {
        return std::unexpected(drained.error());
    }
    const auto retiredAt = Clock::now();
    result.drainMs = milliseconds(retiredAt - drainStart);
    result.throughput =
        config.frames / std::chrono::duration<double>(retiredAt - firstCommit).count();
    for (auto& slot : host.state->slots) {
        if (auto retired = retire(slot); !retired) {
            return std::unexpected(retired.error());
        }
    }
    result.allocatedBytes = host.allocatedBytes();
    if (config.warmup >= kSlots * kPhaseCount && result.allocatedBytes > warmedAllocated) {
        return std::unexpected("Native allocator grew after warmup; invalidate this run");
    }
    result.verified = config.verify;
    return result;
}

//======================================================================================================================
std::string capabilitiesJson() {
    return R"({"schemaVersion":1,"modes":["direct","cpu-indirect","gpu-args","batched"],"gpuSpan":"unavailable","gpuSpanReason":"Workload-boundary ordering has not been proven; no timestamp markers or inferred spans are emitted","icb":{"status":"unresolved","reason":"Bounded pinned-Slang probe rejects render_command with E30015 (undefined identifier). Native creation/execution, GPU generation, reset and reuse remain unverified; this is not proof that every possible Slang encoding is impossible.","reproducer":"ThirdParty/slang/bin/slangc Experiments/GpuSubmission/Shaders/Probe.slang -target metal -o /tmp/LuminexSubmissionProbe.metal","nativeHost":"unresolved","shaderGeneration":"unresolved","probeSyntax":"unsupported"},"memory":{"requested":"GPU resource requests plus CPU replay inputs, result capacity and validation images; all three slots","allocated":"Sum of queried Metal resource allocatedSize and command-allocator high-water bytes; opaque pipeline/table/device allocations excluded","residentBytes":null},"startup":"setupMs measures native object allocation and shader/pipeline loading before warmup; input/oracle generation excluded","drain":"drainMs measures only the final retirement wait and is included in throughput; verification throughput is unscored"})";
}

} // namespace lmx::experimental::submission
