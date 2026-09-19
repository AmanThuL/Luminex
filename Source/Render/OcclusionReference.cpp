//----------------------------------------------------------------------------------------------------------------------
/// @file OcclusionReference.cpp
/// @brief Renders direct candidate IDs and joins retired geometry visibility observations.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/OcclusionReference.h"
#include "Core/Assert.h"
#include <algorithm>
#include <cstddef>
#include <format>
#include <limits>
#include <unordered_map>

namespace lmx::render {
namespace {
struct ReferenceParams {
    glm::mat4 viewProjection;
    uint32_t instanceRow;
    uint32_t padding[3]{};
};
static_assert(sizeof(ReferenceParams) == 80);
static_assert(offsetof(ReferenceParams, instanceRow) == 64);
} // namespace

//======================================================================================================================
rojoRHI::Result<std::unique_ptr<OcclusionReference>> OcclusionReference::create(rojoRHI::Device& device) {
    auto self = std::unique_ptr<OcclusionReference>(new OcclusionReference(device));
    auto library = device.loadShaderLibrary("Shaders/OcclusionReference");
    if (!library)
        return std::unexpected(library.error());
    self->m_library = std::move(*library);
    for (uint32_t wireframe = 0; wireframe < 2; ++wireframe) {
        for (uint32_t masked = 0; masked < 2; ++masked) {
            for (uint32_t doubleSided = 0; doubleSided < 2; ++doubleSided) {
                const uint32_t index = wireframe * 4 + masked * 2 + doubleSided;
                auto pipeline = device.createGraphicsPipeline(
                    {.library = self->m_library.get(),
                     .vertexEntry = "vertexMain",
                     .fragmentEntry = masked ? "fragmentMask" : "fragmentMain",
                     .colorFormat = rojoRHI::Format::RGBA8Unorm,
                     .depthFormat = rojoRHI::Format::D32Float,
                     .depthTestEnable = true,
                     .depthWriteEnable = true,
                     .fillMode = wireframe ? rojoRHI::FillMode::Wireframe : rojoRHI::FillMode::Solid,
                     .cullMode = doubleSided ? rojoRHI::CullMode::None : rojoRHI::CullMode::Back,
                     .depthCompare = rojoRHI::DepthCompare::Greater,
                     .label = std::format("lmx.occlusion.reference.pipeline.{}", index)});
                if (!pipeline)
                    return std::unexpected(pipeline.error());
                self->m_pipelines[index] = std::move(*pipeline);
            }
        }
    }
    auto sampler =
        device.createSampler({.maxAnisotropy = 16, .label = "lmx.occlusion.reference.sampler"});
    if (!sampler)
        return std::unexpected(sampler.error());
    self->m_sampler = std::move(*sampler);
    const std::array<uint8_t, 4> white{255, 255, 255, 255};
    const rojoRHI::TextureMip mip{.data = white.data(), .bytesPerRow = 4};
    auto texture = device.createTexture({.width = 1,
                                         .height = 1,
                                         .format = rojoRHI::Format::RGBA8Unorm,
                                         .sampled = true,
                                         .label = "lmx.occlusion.reference.white"},
                                        std::span{&mip, 1});
    if (!texture)
        return std::unexpected(texture.error());
    self->m_white = std::move(*texture);
    return self;
}

//======================================================================================================================
rojoRHI::Result<void> OcclusionReference::declare(RenderGraph& graph, rojoRHI::CommandList& commands,
                                              const SceneView& view,
                                              const glm::mat4& viewProjection, uint32_t width,
                                              uint32_t height, bool strictView) {
    LMX_ASSERT(width != 0 && height != 0, "reference requires a nonempty active extent");
    const uint64_t frame = m_device.frameNumber();
    LMX_ASSERT(frame > 0, "reference requires an open paced frame");
    if (frame > 3)
        retireThrough(frame - 3);
    auto& slot = m_slots[(frame - 1) % 3];
    const uint64_t byteCount = uint64_t{width} * height * 4;
    if (!slot.buffer || slot.buffer->size() < byteCount) {
        auto buffer = m_device.createBuffer(
            {.size = byteCount, .cpuReadback = true, .label = "lmx.occlusion.reference.readback"},
            nullptr);
        if (!buffer)
            return std::unexpected(buffer.error());
        slot.buffer = std::move(*buffer);
        slot.used = false;
    }
    Pending pending{.frameNumber = frame,
                    .sceneGeneration = view.temporal.sceneGeneration,
                    .width = width,
                    .height = height,
                    .strict = strictView,
                    .buffer = slot.buffer.get()};
    for (const auto& item : view.items) {
        LMX_ASSERT(item.instanceRow != std::numeric_limits<uint32_t>::max(),
                   "reference ID row plus one must not overflow");
        pending.observations.push_back({item.instanceIdentity, item.instanceRow});
    }
    const auto ids = graph.createTexture(
        {.width = width, .height = height, .format = rojoRHI::Format::RGBA8Unorm, .renderTarget = true},
        "occlusionReferenceIds");
    const auto depth = graph.createTexture(
        {.width = width, .height = height, .format = rojoRHI::Format::D32Float, .renderTarget = true},
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
        [this, &commands, view, viewProjection](const PassResources&) {
            if (view.items.empty())
                return;
            commands.bindBuffer(0, *view.tables.vertices);
            commands.bindBuffer(kSceneInstancesSlot, *view.tables.instances);
            commands.bindBuffer(kSceneMaterialsSlot, *view.tables.materials);
            commands.bindSampler(0, *m_sampler);
            for (const auto& item : view.items) {
                LMX_ASSERT(item.instanceRow < view.tables.instanceCount,
                           "reference draw must name a current instance row");
                const bool masked = item.alphaMode == AlphaMode::Mask;
                const bool doubleSided = masked && item.doubleSided;
                commands.bindPipeline(*m_pipelines[(view.wireframe ? 4 : 0) + (masked ? 2 : 0) +
                                                   (doubleSided ? 1 : 0)]);
                commands.bindFrameData(1, ReferenceParams{viewProjection, item.instanceRow});
                commands.bindTexture(0, item.diffuse ? *item.diffuse : *m_white);
                commands.drawIndexed(*view.tables.indices, item.mesh.indexCount,
                                     item.mesh.firstIndex);
            }
        });
    const auto source = nextVersion(ids);
    const auto destination = graph.importBuffer(*slot.buffer, "lmx.occlusion.reference.readback",
                                                slot.used ? rojoRHI::BufferUse::CopyDestination
                                                          : rojoRHI::BufferUse::ShaderRead);
    CopyPassDesc copy;
    copy.textureSources = {source};
    copy.bufferDestinations = {destination};
    graph.addCopyPass(
        "lmx.pass.occlusion.reference.readback", std::move(copy),
        [&commands, source, destination, width, height](const PassResources& resources) {
            const auto texture = resources.texture(source);
            const auto buffer = resources.buffer(destination);
            LMX_ASSERT(texture && buffer, "reference copy resources must be declared");
            commands.copyTextureToBuffer(**texture, {.width = width, .height = height}, **buffer,
                                         {.bytesPerRow = uint64_t{width} * 4});
        });
    graph.readbackBuffer(nextVersion(destination));
    slot.used = true;
    m_pending.push_back(std::move(pending));
    return {};
}

//======================================================================================================================
void OcclusionReference::retireThrough(uint64_t completedFrame) {
    for (auto& pending : m_pending) {
        if (pending.frameNumber > completedFrame)
            continue;
        std::vector<uint8_t> bytes(uint64_t{pending.width} * pending.height * 4);
        pending.buffer->readback(bytes.data(), bytes.size());
        std::unordered_map<uint32_t, size_t> rows;
        for (size_t i = 0; i < pending.observations.size(); ++i)
            rows.emplace(pending.observations[i].instanceRow, i);
        for (size_t i = 0; i < bytes.size(); i += 4) {
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
        pending.buffer = nullptr;
        m_retired.push_back(std::move(pending));
    }
    std::erase_if(m_pending, [completedFrame](const auto& pending) {
        return pending.frameNumber <= completedFrame;
    });
}

//======================================================================================================================
std::optional<OcclusionCheckResult> OcclusionReference::check(const VisibilityStatus& status) {
    const auto found = std::find_if(m_retired.begin(), m_retired.end(), [&status](const auto& p) {
        return p.frameNumber == status.frameNumber && p.sceneGeneration == status.sceneGeneration;
    });
    if (found == m_retired.end())
        return std::nullopt;
    LMX_ASSERT(status.isRetired, "reference joins only retired production visibility");
    std::unordered_map<uint32_t, bool> states;
    for (const auto& candidate : status.scene.candidates)
        states.emplace(candidate.instanceRow, candidate.state == VisibilityState::Rejected &&
                                                  candidate.reason == VisibilityReason::Occluded);
    uint32_t unmatched = 0;
    for (auto& observation : found->observations) {
        const auto state = states.find(observation.instanceRow);
        if (state == states.end())
            ++unmatched;
        else
            observation.occluded = state->second;
    }
    auto result = m_history.observe(status.frameNumber, status.sceneGeneration, found->strict,
                                    found->observations);
    result.invalidReferencePixels = found->invalidPixels;
    result.unmatchedCandidates = unmatched;
    m_retired.erase(found);
    return result;
}
} // namespace lmx::render
