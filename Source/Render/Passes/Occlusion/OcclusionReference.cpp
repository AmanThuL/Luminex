//----------------------------------------------------------------------------------------------------------------------
/// @file OcclusionReference.cpp
/// @brief Renders direct candidate IDs and joins retired geometry visibility observations.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/Passes/Occlusion/OcclusionReference.h"
#include "Core/Diagnostics/Assert.h"
#include "Core/Math/Align.h"
#include "Engine/Gpu/CaptureSchema.h"
#include "Engine/Gpu/GpuOwned.h"
#include "Render/Common/FrameData.h"
#include "Render/Common/StageSetup.h"
#include "Render/Passes/Occlusion/OcclusionRoots.h"
#include "Render/Passes/Visibility/GpuVisibility.h"
#include "Render/Passes/Visibility/VisibilityRetirement.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <limits>
#include <unordered_map>

namespace lmx::render {
//======================================================================================================================
std::shared_ptr<const OcclusionDeclarationAssociation>
createOcclusionDeclarationAssociation(engine::GpuContext& gpu, uint64_t frame,
                                      uint64_t sceneGeneration) {
    return std::make_shared<OcclusionDeclarationAssociation>(gpu, frame, sceneGeneration);
}

//======================================================================================================================
OcclusionReference::OcclusionReference(engine::GpuContext& gpu) : m_gpu(gpu) {}

//======================================================================================================================
OcclusionReference::~OcclusionReference() = default;

//======================================================================================================================
lmx::Result<std::unique_ptr<OcclusionReference>>
OcclusionReference::create(engine::GpuContext& gpu) {
    auto device = gpu.device();
    auto self = std::unique_ptr<OcclusionReference>(new OcclusionReference(gpu));
    auto library = gpu.loadShaderLibrary("OcclusionReference");
    if (!library)
        return std::unexpected(library.error());
    self->m_library = std::move(*library);
    const std::array<rojoRHI::ColorTarget, 1> colors{{{.format = rojoRHI::Format::RGBA8Unorm}}};
    for (uint32_t wireframe = 0; wireframe < 2; ++wireframe) {
        for (uint32_t masked = 0; masked < 2; ++masked) {
            for (uint32_t doubleSided = 0; doubleSided < 2; ++doubleSided) {
                const uint32_t index = wireframe * 4 + masked * 2 + doubleSided;
                auto pipeline = engine::adoptGpu(
                    device,
                    device.createPipeline(rojoRHI::GraphicsPipelineDesc{
                        .vertex = {self->m_library.get(), "vertexMain"},
                        .fragment = {self->m_library.get(),
                                     masked ? "fragmentMask" : "fragmentMain"},
                        .raster = {.cull = doubleSided ? rojoRHI::CullMode::None
                                                       : rojoRHI::CullMode::Back,
                                   .fill = wireframe ? rojoRHI::FillMode::Wireframe
                                                     : rojoRHI::FillMode::Solid},
                        .depthStencil = {.depthTest = true,
                                         .depthWrite = true,
                                         .depthCompare = rojoRHI::CompareOp::Greater},
                        .colors = colors,
                        .depthFormat = rojoRHI::Format::D32Float,
                        .label = std::format("lmx.occlusion.reference.pipeline.{}", index)}));
                if (!pipeline)
                    return std::unexpected(pipeline.error());
                self->m_pipelines[index] = std::move(*pipeline);
            }
        }
    }
    auto sampler = engine::adoptGpu(
        device,
        device.createSampler({.maxAnisotropy = 16, .label = "lmx.occlusion.reference.sampler"}));
    if (!sampler)
        return std::unexpected(sampler.error());
    self->m_sampler = std::move(*sampler);
    const std::array<uint8_t, 4> white{255, 255, 255, 255};
    auto texture = createTexel(device, rojoRHI::Format::RGBA8Unorm, rojoRHI::TextureType::Tex2D,
                               std::as_bytes(std::span{white}), "lmx.occlusion.reference.white");
    if (!texture)
        return std::unexpected(texture.error());
    self->m_white = std::move(*texture);
    engine::registerUniformStruct(
        {"OcclusionReferenceRoot",
         sizeof(OcclusionReferenceRoot),
         {{"verticesAddr", offsetof(OcclusionReferenceRoot, verticesAddr), "Addr"},
          {"instancesAddr", offsetof(OcclusionReferenceRoot, instancesAddr), "Addr"},
          {"materialsAddr", offsetof(OcclusionReferenceRoot, materialsAddr), "Addr"},
          {"paramsAddr", offsetof(OcclusionReferenceRoot, paramsAddr), "Addr"},
          {"diffuse", offsetof(OcclusionReferenceRoot, diffuse), "uint"},
          {"sampler", offsetof(OcclusionReferenceRoot, sampler), "uint"},
          {"padding0", offsetof(OcclusionReferenceRoot, padding0), "uint"},
          {"padding1", offsetof(OcclusionReferenceRoot, padding1), "uint"}}});
    engine::registerUniformStruct(
        {"ReferenceParams",
         sizeof(ReferenceParams),
         {{"viewProjection", offsetof(ReferenceParams, viewProjection), "float4x4"},
          {"instanceRow", offsetof(ReferenceParams, instanceRow), "uint"},
          {"padding0", offsetof(ReferenceParams, padding0), "uint"},
          {"padding1", offsetof(ReferenceParams, padding1), "uint"},
          {"padding2", offsetof(ReferenceParams, padding2), "uint"}}});
    return self;
}

//======================================================================================================================
lmx::Result<void> OcclusionReference::declare(RenderGraph& graph, rojoRHI::CommandList& commands,
                                              const SceneView& view,
                                              const OcclusionReferenceInputs& inputs) {
    return declareImpl(graph, commands, view, inputs, true);
}

//======================================================================================================================
Result<void> OcclusionReference::declareCombined(RenderGraph& graph, rojoRHI::CommandList& commands,
                                                 const SceneView& view,
                                                 const OcclusionReferenceInputs& inputs) {
    return declareImpl(graph, commands, view, inputs, false);
}

//======================================================================================================================
Result<void> OcclusionReference::declareImpl(RenderGraph& graph, rojoRHI::CommandList& commands,
                                             const SceneView& view,
                                             const OcclusionReferenceInputs& inputs,
                                             bool autoRetire) {
    const auto& viewProjection = inputs.viewProjection;
    const auto width = inputs.width;
    const auto height = inputs.height;
    const auto strictView = inputs.strictView;
    LMX_ASSERT(width != 0 && height != 0, "reference requires a nonempty active extent");
    const uint64_t frame = m_gpu.frameNumber();
    LMX_ASSERT(frame > 0, "reference requires an open paced frame");
    if (autoRetire && frame > 3) {
        auto retired = retireThrough(frame - 3);
        if (!retired)
            return std::unexpected(retired.error());
    }
    const uint64_t pitch = alignUp(uint64_t{width} * 4, uint64_t{256});
    if (pitch > std::numeric_limits<uint32_t>::max() ||
        height > std::numeric_limits<uint64_t>::max() / pitch)
        return std::unexpected(
            Error{ErrorCode::InvalidArgument, "reference readback extent exceeds its copy layout"});
    const uint64_t byteCount = pitch * height;
    auto& cached = m_slots[(frame - 1) % 3];
    const bool reusable = cached && cached.use_count() == 1;
    auto backing = reusable ? cached : std::make_shared<ReferenceBacking>();
    const uint64_t capacity = std::max(byteCount, cached ? cached->capacity : uint64_t{0});
    if (!backing->buffer || backing->capacity < capacity) {
        auto buffer = engine::GpuBuffer::create(m_gpu.device(),
                                                {.size = capacity,
                                                 .memory = rojoRHI::Memory::Readback,
                                                 .label = "lmx.occlusion.reference.readback"});
        if (!buffer)
            return std::unexpected(buffer.error());
        backing->buffer = std::move(*buffer);
        backing->capacity = capacity;
        backing->physical = {rojoRHI::Stage::None, rojoRHI::Access::None,
                             rojoRHI::Layout::Undefined};
    }
    const auto recording = std::make_shared<DeclarationRecordingState>();
    const auto callbackAnchor = std::make_shared<const uint8_t>(0);
    const auto check = [this, frame, recording, callbackAnchor] {
        (void)callbackAnchor;
        LMX_ASSERT(!recording->discarded && m_gpu.inFrame() && m_gpu.frameNumber() == frame,
                   "reference callback requires its live declaring context frame");
        recording->entered = true;
        ++m_epoch;
    };
    ReferencePendingReadback pending{.frameNumber = frame,
                                     .sceneGeneration = view.temporal.sceneGeneration,
                                     .width = width,
                                     .height = height,
                                     .rowPitch = static_cast<uint32_t>(pitch),
                                     .strict = strictView,
                                     .recording = recording,
                                     .callbackAnchor = callbackAnchor,
                                     .association = inputs.association,
                                     .backing = backing};
    for (const auto& item : view.items) {
        LMX_ASSERT(item.instanceRow < view.tables.instanceRows.size(),
                   "reference observations require canonical instance rows");
        if (view.tables.instanceRows[item.instanceRow].flags & engine::kInstanceDisabled)
            continue;
        LMX_ASSERT(item.instanceRow != std::numeric_limits<uint32_t>::max(),
                   "reference ID row plus one must not overflow");
        pending.observations.push_back({item.instanceIdentity, item.instanceRow});
    }
    const auto ids = graph.createTexture({.width = width,
                                          .height = height,
                                          .format = rojoRHI::Format::RGBA8Unorm,
                                          .renderTarget = true},
                                         "occlusionReferenceIds");
    const auto depth = graph.createTexture({.width = width,
                                            .height = height,
                                            .format = rojoRHI::Format::D32Float,
                                            .renderTarget = true},
                                           "occlusionReferenceDepth");
    PassDesc pass;
    if (!view.items.empty()) {
        LMX_ASSERT(view.tables.vertices && view.tables.indices && view.tables.instances &&
                       view.tables.materials,
                   "reference draws require scene table bindings");
        pass.bufferReads = {
            graph.importBuffer(*view.tables.vertices, "lmx.occlusion.reference.vertices"),
            graph.importBuffer(*view.tables.indices, "lmx.occlusion.reference.indices"),
            graph.importBuffer(*view.tables.instances, "lmx.occlusion.reference.instances"),
            graph.importBuffer(*view.tables.materials, "lmx.occlusion.reference.materials")};
    }
    pass.color = ColorAttachment{.handle = ids, .clearColor = {0, 0, 0, 0}};
    pass.depth = DepthAttachment{
        .handle = depth, .load = LoadOp::Clear, .store = StoreOp::Discard, .clearDepth = 0.0f};
    graph.addPass(
        "lmx.pass.occlusion.reference", std::move(pass),
        [this, &commands, view, viewProjection, check](const PassResources&) {
            check();
            for (const auto& item : view.items) {
                LMX_ASSERT(item.instanceRow < view.tables.instanceCount,
                           "reference draw must name a current instance row");
                if (view.tables.instanceRows[item.instanceRow].flags & engine::kInstanceDisabled)
                    continue;
                const bool masked = item.alphaMode == engine::AlphaMode::Mask;
                const bool doubleSided = masked && item.doubleSided;
                commands.setPipeline(
                    m_pipelines[(view.wireframe ? 4 : 0) + (masked ? 2 : 0) + (doubleSided ? 1 : 0)]
                        .get());
                const auto params =
                    frameData(commands, ReferenceParams{viewProjection, item.instanceRow});
                engine::registerFrameRoot("ReferenceParams", params);
                const auto* diffuse = item.diffuse ? item.diffuse : &m_white;
                engine::registerFrameRoot(
                    "OcclusionReferenceRoot",
                    commands.pushRoot(OcclusionReferenceRoot{
                        view.tables.vertices->address(), view.tables.instances->address(),
                        view.tables.materials->address(), params,
                        static_cast<uint32_t>(diffuse->view()),
                        static_cast<uint32_t>(m_sampler.get()), 0, 0}));
#ifndef NDEBUG
                if (item.mesh.indexCount) {
                    std::vector<rojoRHI::ResourceAccess> accesses{
                        {.buffer = view.tables.vertices->rhi(),
                         .size = view.tables.vertices->desc().size,
                         .stage = rojoRHI::Stage::VertexShader,
                         .access = rojoRHI::Access::ShaderRead},
                        {.buffer = view.tables.instances->rhi(),
                         .offset = uint64_t{item.instanceRow} * sizeof(engine::InstanceRow),
                         .size = sizeof(engine::InstanceRow),
                         .stage = rojoRHI::Stage::VertexShader,
                         .access = rojoRHI::Access::ShaderRead},
                        {.buffer = view.tables.materials->rhi(),
                         .size = uint64_t{view.tables.materialCount} * sizeof(engine::MaterialRow),
                         .stage = rojoRHI::Stage::VertexShader,
                         .access = rojoRHI::Access::ShaderRead}};
                    if (masked) {
                        accesses.push_back(
                            {.buffer = view.tables.instances->rhi(),
                             .offset = uint64_t{item.instanceRow} * sizeof(engine::InstanceRow),
                             .size = sizeof(engine::InstanceRow),
                             .stage = rojoRHI::Stage::FragmentShader,
                             .access = rojoRHI::Access::ShaderRead});
                        accesses.push_back({.buffer = view.tables.materials->rhi(),
                                            .size = uint64_t{view.tables.materialCount} *
                                                    sizeof(engine::MaterialRow),
                                            .stage = rojoRHI::Stage::FragmentShader,
                                            .access = rojoRHI::Access::ShaderRead});
                        accesses.push_back({.texture = diffuse->rhi(),
                                            .stage = rojoRHI::Stage::FragmentShader,
                                            .access = rojoRHI::Access::ShaderRead});
                    }
                    commands.declareAccess(accesses);
                }
#endif
                commands.drawIndexed(view.tables.indices->rhi(), rojoRHI::IndexType::UInt32,
                                     item.mesh.indexCount, 1, item.mesh.firstIndex, 0, 0);
            }
        });
    const auto source = nextVersion(ids);
    const auto destination = graph.importBuffer(backing->buffer, "lmx.occlusion.reference.readback",
                                                BufferUse::CopyDestination, backing->physical);
    CopyPassDesc copy;
    copy.textureSources = {source};
    copy.bufferDestinations = {destination};
    const auto ticket = pending.ticket;
    graph.addCopyPass("lmx.pass.occlusion.reference.readback", std::move(copy),
                      [this, &commands, source, destination, width, height, pitch, byteCount,
                       ticket, backing, check](const PassResources& resources) {
                          check();
                          commands.copyTextureToBuffer(resources.texture(source).texture,
                                                       {.extent = {width, height, 1}},
                                                       resources.buffer(destination),
                                                       {.rowPitch = static_cast<uint32_t>(pitch)});
                          backing->physical = {rojoRHI::Stage::Copy, rojoRHI::Access::CopyWrite,
                                               rojoRHI::Layout::Undefined};
                          *ticket = recordReadback(m_gpu, commands, backing->buffer,
                                                   backing->physical, {0, byteCount});
                      });
    graph.readbackBuffer(nextVersion(destination));
    cached = std::move(backing);
    m_pending.push_back(std::move(pending));
    ++m_epoch;
    return {};
}

//======================================================================================================================
Result<ReferenceRetirementBatch> prepareReferenceRetirement(
    engine::GpuContext& gpu, const std::vector<ReferencePendingReadback>& pendingEntries,
    const std::vector<ReferencePendingReadback>& retired, const OcclusionCheckHistory& history,
    uint64_t completedFrame, std::span<const VisibilityStatus> statuses) {
    ReferenceRetirementBatch staged;
    staged.ready = retired;
    staged.history = history;
    staged.results.assign(statuses.begin(), statuses.end());
    for (size_t index = 0; index < pendingEntries.size(); ++index) {
        const auto& entry = pendingEntries[index];
        if (entry.frameNumber > completedFrame)
            continue;
        staged.retiring.push_back(index);
        if (!entry.ticket || !*entry.ticket) {
            if ((entry.recording && entry.recording->entered) || !entry.callbackAnchor.expired())
                return std::unexpected(Error{ErrorCode::InvalidArgument,
                                             "reference recording is incomplete or borrowed"});
            if (entry.recording)
                staged.discards.push_back(entry.recording);
            continue;
        }
        auto pending = entry;
        std::vector<uint8_t> bytes(uint64_t{pending.rowPitch} * pending.height);
        const auto resolved = resolve(gpu, **pending.ticket, bytes);
        if (!resolved)
            return std::unexpected(resolved.error());
        const auto& ticket = **pending.ticket;
        LMX_ASSERT(ticket.frame == pending.frameNumber &&
                       ticket.buffer == pending.backing->buffer.rhi() && ticket.texture.bits == 0 &&
                       ticket.region.offset == 0 && ticket.region.size == bytes.size(),
                   "reference ticket identity differs from its retained copy");
        std::unordered_map<uint32_t, size_t> rows;
        for (size_t i = 0; i < pending.observations.size(); ++i)
            rows.emplace(pending.observations[i].instanceRow, i);
        for (uint32_t y = 0; y < pending.height; ++y) {
            for (uint32_t x = 0; x < pending.width; ++x) {
                const auto i = uint64_t{y} * pending.rowPitch + uint64_t{x} * 4;
                const uint32_t id = uint32_t{bytes[i]} | (uint32_t{bytes[i + 1]} << 8) |
                                    (uint32_t{bytes[i + 2]} << 16) | (uint32_t{bytes[i + 3]} << 24);
                if (id == 0)
                    continue;
                const auto found = rows.find(id - 1);
                if (found == rows.end())
                    ++pending.invalidPixels;
                else
                    ++pending.observations[found->second].visiblePixels;
            }
        }
        pending.ticket.reset();
        staged.ready.push_back(std::move(pending));
    }
    for (auto& status : staged.results) {
        if (!status.occlusionCheckEnabled || status.occlusionCheck.enabled)
            continue;
        const auto& association = status.occlusionAssociation;
        if (!association || association->context != &gpu ||
            association->frame != status.frameNumber ||
            association->sceneGeneration != status.sceneGeneration)
            return std::unexpected(Error{ErrorCode::InvalidArgument,
                                         "checked visibility has no matching declaration"});
        auto checked = checkReference(staged.ready, staged.history, status);
        if (!checked)
            return std::unexpected(checked.error());
        if (!*checked)
            return std::unexpected(
                Error{ErrorCode::InvalidArgument, "checked visibility reference is incomplete"});
        status.occlusionCheck = std::move(**checked);
    }
    return staged;
}

//======================================================================================================================
void commitReferenceRetirement(ReferenceRetirementBatch& staged,
                               std::vector<ReferencePendingReadback>& pending,
                               std::vector<ReferencePendingReadback>& retired,
                               OcclusionCheckHistory& history) noexcept {
    for (const auto& recording : staged.discards)
        recording->discarded = true;
    eraseRetiring(pending, staged.retiring);
    retired.swap(staged.ready);
    std::swap(history, staged.history);
}

//======================================================================================================================
Result<void> retireReferenceThrough(engine::GpuContext& gpu,
                                    std::vector<ReferencePendingReadback>& pending,
                                    std::vector<ReferencePendingReadback>& retired,
                                    uint64_t completedFrame) {
    OcclusionCheckHistory unusedHistory;
    auto staged =
        prepareReferenceRetirement(gpu, pending, retired, unusedHistory, completedFrame, {});
    if (!staged)
        return std::unexpected(staged.error());
    commitReferenceRetirement(*staged, pending, retired, unusedHistory);
    return {};
}

//======================================================================================================================
Result<std::optional<OcclusionCheckResult>>
checkReference(std::vector<ReferencePendingReadback>& retired, OcclusionCheckHistory& history,
               const VisibilityStatus& status) {
    const auto found = std::find_if(retired.begin(), retired.end(), [&status](const auto& p) {
        return status.occlusionAssociation && p.association == status.occlusionAssociation &&
               p.frameNumber == status.frameNumber && p.sceneGeneration == status.sceneGeneration;
    });
    if (found == retired.end())
        return std::optional<OcclusionCheckResult>{};
    LMX_ASSERT(status.isRetired && found->association->frame == found->frameNumber &&
                   found->association->sceneGeneration == found->sceneGeneration,
               "reference joins only the exact retired production declaration");
    auto observations = found->observations;
    auto stagedHistory = history;
    std::unordered_map<uint32_t, bool> states;
    for (const auto& candidate : status.scene.candidates)
        states.emplace(candidate.instanceRow, candidate.state == VisibilityState::Rejected &&
                                                  candidate.reason == VisibilityReason::Occluded);
    uint32_t unmatched = 0;
    for (auto& observation : observations) {
        const auto state = states.find(observation.instanceRow);
        if (state == states.end())
            ++unmatched;
        else
            observation.occluded = state->second;
    }
    auto result = stagedHistory.observe(status.frameNumber, status.sceneGeneration, found->strict,
                                        observations);
    result.invalidReferencePixels = found->invalidPixels;
    result.unmatchedCandidates = unmatched;
    retired.erase(found);
    std::swap(history, stagedHistory);
    return std::optional{std::move(result)};
}

//======================================================================================================================
Result<PreparedReferenceJoin>
OcclusionReference::prepareJoin(uint64_t completedFrame,
                                std::span<const VisibilityStatus> statuses) const {
    auto batch = prepareReferenceRetirement(m_gpu, m_pending, m_retired, m_history, completedFrame,
                                            statuses);
    if (!batch)
        return std::unexpected(batch.error());
    PreparedReferenceJoin prepared;
    prepared.m_owner = this;
    prepared.m_context = &m_gpu;
    prepared.m_epoch = m_epoch;
    prepared.m_batch = std::move(*batch);
    return prepared;
}

//======================================================================================================================
void OcclusionReference::validateJoin(const PreparedReferenceJoin& prepared) const {
    // One message per failure, so each misuse is distinguishable from the others.
    LMX_ASSERT(prepared.m_owner != nullptr, "reference prepared join is consumed");
    LMX_ASSERT(prepared.m_owner == this && prepared.m_context == &m_gpu,
               "reference prepared join is foreign");
    LMX_ASSERT(prepared.m_epoch == m_epoch, "reference prepared join is stale");
}

//======================================================================================================================
void OcclusionReference::commitJoin(PreparedReferenceJoin&& prepared) noexcept {
    validateJoin(prepared);
    commitReferenceRetirement(prepared.m_batch, m_pending, m_retired, m_history);
    prepared.m_owner = nullptr;
    ++m_epoch;
}

//======================================================================================================================
Result<void> OcclusionReference::retireThrough(uint64_t completedFrame) {
    auto staged = prepareJoin(completedFrame, {});
    if (!staged)
        return std::unexpected(staged.error());
    commitJoin(std::move(*staged));
    return {};
}

//======================================================================================================================
Result<std::optional<OcclusionCheckResult>>
OcclusionReference::check(const VisibilityStatus& status) {
    auto result = checkReference(m_retired, m_history, status);
    if (result && *result)
        ++m_epoch;
    return result;
}

//======================================================================================================================
Result<std::vector<VisibilityStatus>> takeRetiredVisibilityWithReference(
    engine::GpuContext& gpu, std::vector<VisibilityPendingReadback>& pending,
    std::vector<VisibilityStatus>& retired, std::vector<ReferencePendingReadback>* referencePending,
    std::vector<ReferencePendingReadback>* referenceRetired, OcclusionCheckHistory* history,
    uint64_t completedFrame) {
    auto production = prepareVisibilityRetirement(gpu, pending, retired, completedFrame);
    if (!production)
        return std::unexpected(production.error());
    std::optional<ReferenceRetirementBatch> reference;
    if (referencePending) {
        LMX_ASSERT(referenceRetired && history, "reference owner must retain all join state");
        auto prepared = prepareReferenceRetirement(gpu, *referencePending, *referenceRetired,
                                                   *history, completedFrame, production->ready);
        if (!prepared)
            return std::unexpected(prepared.error());
        reference = std::move(*prepared);
        adoptJoinedStatuses(production->ready, reference->results);
    } else if (std::ranges::any_of(production->ready, [](const auto& status) {
                   return status.occlusionCheckEnabled && !status.occlusionCheck.enabled;
               })) {
        return std::unexpected(
            Error{ErrorCode::InvalidArgument, "checked visibility has no reference owner"});
    }
    prepareVisibilityProjections(*production);
    if (reference)
        commitReferenceRetirement(*reference, *referencePending, *referenceRetired, *history);
    commitVisibilityRetirement(*production, pending, retired);
    auto results = std::move(retired);
    retired.clear();
    return results;
}

//======================================================================================================================
Result<std::vector<VisibilityStatus>>
VisibilityRetirementAccess::finish(GpuVisibility& production, OcclusionReference* reference,
                                   uint64_t completedFrame, bool consume) {
    auto prepared = production.prepareRetiredTake(completedFrame);
    if (!prepared)
        return std::unexpected(prepared.error());
    std::optional<PreparedReferenceJoin> joined;
    if (reference) {
        LMX_ASSERT(&reference->m_gpu == &production.m_gpu,
                   "reference and production must share their creating context");
        auto result = reference->prepareJoin(completedFrame, prepared->statuses());
        if (!result)
            return std::unexpected(result.error());
        joined = std::move(*result);
        adoptJoinedStatuses(prepared->m_batch.ready, joined->m_batch.results);
    } else if (std::ranges::any_of(prepared->statuses(), [](const auto& status) {
                   return status.occlusionCheckEnabled && !status.occlusionCheck.enabled;
               })) {
        return std::unexpected(
            Error{ErrorCode::InvalidArgument, "checked visibility has no reference owner"});
    }
    prepareVisibilityProjections(prepared->m_batch);
    // Both epochs and every Result-bearing operation are checked before either publication.
    production.validateRetiredTake(*prepared);
    if (joined)
        reference->validateJoin(*joined);
    if (joined)
        reference->commitJoin(std::move(*joined));
    auto output = production.commitRetiredTake(std::move(*prepared));
    if (!consume) {
        production.m_retired.swap(output);
        return std::vector<VisibilityStatus>{};
    }
    return output;
}

//======================================================================================================================
Result<void> VisibilityRetirementAccess::retire(GpuVisibility& production,
                                                OcclusionReference* reference,
                                                uint64_t completedFrame) {
    auto result = finish(production, reference, completedFrame, false);
    if (!result)
        return std::unexpected(result.error());
    return {};
}

//======================================================================================================================
Result<std::vector<VisibilityStatus>>
VisibilityRetirementAccess::take(GpuVisibility& production, OcclusionReference* reference,
                                 uint64_t completedFrame) {
    return finish(production, reference, completedFrame, true);
}
} // namespace lmx::render
