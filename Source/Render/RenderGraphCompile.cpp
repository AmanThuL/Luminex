//----------------------------------------------------------------------------------------------------------------------
/// @file RenderGraphCompile.cpp
/// @brief Compiles schedules and assigns transient lifetimes and placement.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/RenderGraph.h"
#include "Render/RenderGraphInternal.h"

#include "Core/Align.h"
#include "Core/Diagnostics/Assert.h"

#include <algorithm>
#include <format>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lmx::render {
using graph_detail::fail;
using graph_detail::sinkVerb;

namespace {

// Everything two transients must agree on before one may take the other's bytes (spec 11).
//
// Equality on every axis rather than a subset is deliberate conservatism: a placement that reuses
// memory across differing layouts depends on how the driver tiles each one, which is exactly the
// thing a heap does not promise. Storage mode is absent because it is not a variable -- a transient
// is device-private by construction, since it can neither be uploaded to nor read back.
struct AliasClass {
    bool isTexture = true;
    rojoRHI::Format format = rojoRHI::Format::Unknown;
    rojoRHI::TextureKind kind = rojoRHI::TextureKind::Tex2D;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipLevels = 0;
    bool renderTarget = false;
    bool sampled = false;
    bool storageRead = false;
    bool storageWrite = false;
    uint64_t bufferSize = 0;
    // The backend's own answer for the descriptor, which is what the packing is actually built on.
    uint64_t size = 0;
    uint64_t alignment = 0;
    friend bool operator==(const AliasClass&, const AliasClass&) = default;
};

//======================================================================================================================
AliasClass aliasClassOf(bool isTexture, rojoRHI::Format format, const TransientTextureDesc& texture,
                        const TransientBufferDesc& buffer, const rojoRHI::SizeAlign& footprint) {
    if (!isTexture) {
        return {.isTexture = false,
                .storageRead = buffer.storageRead,
                .storageWrite = buffer.storageWrite,
                .bufferSize = buffer.size,
                .size = footprint.size,
                .alignment = footprint.alignment};
    }
    return {.isTexture = true,
            .format = format,
            .kind = texture.kind,
            .width = texture.width,
            .height = texture.height,
            .mipLevels = texture.mipLevels,
            .renderTarget = texture.renderTarget,
            .sampled = texture.sampled,
            .storageRead = texture.storageRead,
            .storageWrite = texture.storageWrite,
            .size = footprint.size,
            .alignment = footprint.alignment};
}

//======================================================================================================================
// A (resource, version) pair as one hashable key. Both halves are 32-bit, so the pair is lossless.
uint64_t versionKey(uint32_t resource, uint32_t version) {
    return (static_cast<uint64_t>(resource) << 32) | version;
}

} // namespace

//======================================================================================================================
GraphResult<Schedule> RenderGraph::compile() const {
    GraphResult<CompiledFrameRecord> record = compileFrame(0);
    if (!record) {
        return std::unexpected(record.error());
    }
    return std::move(record->debug.schedule);
}

//======================================================================================================================
GraphResult<CompiledFrameRecord> RenderGraph::compileFrame(uint64_t frameId) const {
    if (const GraphResult<void> declarations = validateDeclarations(); !declarations) {
        return std::unexpected(declarations.error());
    }

    // Who writes which version, and therefore who produces the version after it. One writer per
    // version is what makes "version v + 1" name a single set of contents -- including when the two
    // writers name disjoint subresources, because the second one still has to say which contents it
    // starts from, and only the version says that.
    std::unordered_map<uint64_t, uint32_t> writerOfVersion;
    std::unordered_map<uint64_t, uint32_t> producerOfVersion;
    for (uint32_t pass = 0; pass < m_passes.size(); ++pass) {
        for (const Declaration& declaration : m_passes[pass].declarations) {
            if (!declaration.isWrite) {
                continue;
            }
            const Resource& resource = m_resources[declaration.resource];
            const auto existing =
                writerOfVersion.find(versionKey(declaration.resource, declaration.version));
            if (existing != writerOfVersion.end()) {
                return fail(
                    std::format("passes '{}' and '{}' both write {} '{}' version {}",
                                m_passes[existing->second].label, m_passes[pass].label,
                                resource.kind == ResourceKind::Texture ? "texture" : "buffer",
                                resource.name, declaration.version));
            }
            writerOfVersion.emplace(versionKey(declaration.resource, declaration.version), pass);
            const bool discarded =
                (declaration.role == UseRole::ColorAttachment &&
                 colorAttachmentOf(m_passes[pass], declaration).store == StoreOp::Discard) ||
                (declaration.role == UseRole::DepthAttachment &&
                 m_passes[pass].depth->store == StoreOp::Discard);
            if (discarded) {
                continue;
            }
            producerOfVersion.emplace(versionKey(declaration.resource, declaration.version + 1),
                                      pass);
        }
    }

    // Every version a pass names must exist, and naming it is the dependency edge. Version 0 is the
    // imported contents and needs no producer; a write is an edge too, because taking a resource to
    // its next version has to happen after whatever put it in the state being taken.
    std::vector<std::vector<uint32_t>> consumers(m_passes.size());
    std::vector<uint32_t> pendingDependencies(m_passes.size(), 0);
    for (uint32_t pass = 0; pass < m_passes.size(); ++pass) {
        for (const Declaration& declaration : m_passes[pass].declarations) {
            if (declaration.version == 0) {
                continue;
            }
            const Resource& resource = m_resources[declaration.resource];
            const auto producer =
                producerOfVersion.find(versionKey(declaration.resource, declaration.version));
            if (producer == producerOfVersion.end()) {
                return fail(
                    std::format("pass '{}' declares a {} of {} '{}' version {}, which no "
                                "pass writes",
                                m_passes[pass].label, roleName(declaration.role),
                                resource.kind == ResourceKind::Texture ? "texture" : "buffer",
                                resource.name, declaration.version));
            }
            consumers[producer->second].push_back(pass);
            ++pendingDependencies[pass];
        }
    }

    for (const Sink& sink : m_sinks) {
        const Resource& resource = m_resources[sink.resource];
        if (!producerOfVersion.contains(versionKey(sink.resource, sink.version))) {
            return fail(
                std::format("{} {} '{}' version {} is not written by any pass", sinkVerb(sink.kind),
                            sink.resourceKind == ResourceKind::Texture ? "texture" : "buffer",
                            resource.name, sink.version));
        }
    }

    // Reverse reachability from the declared sinks, and from nothing else. A pass is live when a
    // sink names a version it produced, or when a live pass names one; a live pass's own reads pull
    // in whatever produced them, which is what carries liveness back down a chain.
    std::vector<bool> live(m_passes.size(), false);
    std::vector<uint32_t> reachable;
    const auto reach = [&](uint32_t resource, uint32_t version) {
        const auto producer = producerOfVersion.find(versionKey(resource, version));
        if (producer != producerOfVersion.end() && !live[producer->second]) {
            live[producer->second] = true;
            reachable.push_back(producer->second);
        }
    };
    for (const Sink& sink : m_sinks) {
        reach(sink.resource, sink.version);
    }
    while (!reachable.empty()) {
        const uint32_t pass = reachable.back();
        reachable.pop_back();
        for (const Declaration& declaration : m_passes[pass].declarations) {
            if (declaration.version > 0) {
                reach(declaration.resource, declaration.version);
            }
        }
    }

    // Kahn's algorithm, taking the lowest-numbered ready pass each round: that is the declaration
    // order tie-break, and it makes the schedule a function of the declarations alone. It runs over
    // every declared pass rather than the live ones, because a cycle is a property of the frame as
    // declared -- culling a cycle away would report a frame as valid that is not.
    Schedule schedule;
    std::vector<bool> scheduled(m_passes.size(), false);
    size_t ordered = 0;
    while (ordered < m_passes.size()) {
        uint32_t ready = static_cast<uint32_t>(m_passes.size());
        for (uint32_t pass = 0; pass < m_passes.size(); ++pass) {
            if (!scheduled[pass] && pendingDependencies[pass] == 0) {
                ready = pass;
                break;
            }
        }
        if (ready == m_passes.size()) {
            std::string involved;
            for (uint32_t pass = 0; pass < m_passes.size(); ++pass) {
                if (scheduled[pass]) {
                    continue;
                }
                involved += involved.empty() ? "" : ", ";
                involved += std::format("'{}'", m_passes[pass].label);
            }
            return fail(std::format("render graph contains a cycle involving passes {}", involved));
        }
        scheduled[ready] = true;
        ++ordered;
        if (live[ready]) {
            schedule.passes.push_back(ready);
        }
        for (uint32_t consumer : consumers[ready]) {
            --pendingDependencies[consumer];
        }
    }

    CompiledFrameRecord record{.frameId = frameId, .debug = {}};
    record.debug.resources.reserve(m_resources.size());
    for (const Resource& resource : m_resources) {
        record.debug.resources.push_back({.name = resource.name,
                                          .kind = resource.kind == ResourceKind::Texture
                                                      ? GraphResourceKind::Texture
                                                      : GraphResourceKind::Buffer,
                                          .format = resource.format});
    }
    record.debug.sinks.reserve(m_sinks.size());
    for (const Sink& sink : m_sinks) {
        record.debug.sinks.push_back({.kind = sink.kind,
                                      .resourceKind = sink.resourceKind == ResourceKind::Texture
                                                          ? GraphResourceKind::Texture
                                                          : GraphResourceKind::Buffer,
                                      .resource = sink.resource,
                                      .version = sink.version});
    }
    record.debug.passes.reserve(m_passes.size());
    for (uint32_t index = 0; index < m_passes.size(); ++index) {
        const Pass& pass = m_passes[index];
        DebugPass entry{.label = pass.label,
                        .kind = pass.kind,
                        .uses = {},
                        .cullReason = {},
                        .renderAreaWidth = pass.renderAreaWidth,
                        .renderAreaHeight = pass.renderAreaHeight};
        entry.uses.reserve(pass.declarations.size());
        bool writes = false;
        for (const Declaration& declaration : pass.declarations) {
            writes = writes || declaration.isWrite;
            entry.uses.push_back({.resource = declaration.resource,
                                  .version = declaration.version,
                                  .role = declaration.role,
                                  .range = declaration.range});
        }
        if (!live[index]) {
            entry.cullReason = writes ? CullReason::NoSinkReachesIt : CullReason::ProducesNothing;
        }
        record.debug.passes.push_back(std::move(entry));
    }
    const AliasPlan plan = planTransients(schedule);
    record.debug.transitions = deriveTransitions(schedule, plan);
    record.debug.transients.reserve(plan.transients.size());
    for (const TransientPlan& entry : plan.transients) {
        record.debug.transients.push_back(
            {.resource = entry.resource,
             .used = entry.used,
             // Positions are how lifetimes are compared; pass indices are how a reader names a
             // pass, so the record carries what the rest of it is written in.
             .firstPass = entry.used ? schedule.passes[entry.firstPosition] : 0,
             .lastPass = entry.used ? schedule.passes[entry.lastPosition] : 0,
             .offset = entry.offset,
             .size = entry.size,
             .alignment = entry.alignment,
             .aliases = entry.aliases});
    }
    record.debug.memory = plan.memory;
    record.debug.poolingEnabled = m_poolingEnabled;
    record.debug.schedule = std::move(schedule);
    return record;
}

//======================================================================================================================
RenderGraph::AliasPlan RenderGraph::planTransients(const Schedule& schedule) const {
    AliasPlan plan;

    // Lifetimes are intervals over execution order, not declaration order, so a pass's position in
    // the schedule is what an interval is measured in. A culled pass has no position at all, which
    // is exactly right: a transient only its culled readers named is one the frame does not
    // allocate.
    std::vector<AliasClass> classes;
    for (uint32_t index = 0; index < m_resources.size(); ++index) {
        const Resource& resource = m_resources[index];
        if (!resource.transient) {
            continue;
        }
        TransientPlan entry{.resource = index};
        for (uint32_t position = 0; position < schedule.passes.size(); ++position) {
            bool touches = false;
            for (const Declaration& declaration :
                 m_passes[schedule.passes[position]].declarations) {
                touches = touches || declaration.resource == index;
            }
            if (!touches) {
                continue;
            }
            entry.firstPosition = entry.used ? entry.firstPosition : position;
            entry.lastPosition = position;
            entry.used = true;
        }

        AliasClass klass;
        if (entry.used) {
            // The RHI is asked what the descriptor costs rather than the descriptor being measured
            // here: only the backend knows the layout it will choose, and a plan built on a guess
            // would place resources where they do not fit.
            rojoRHI::Device& device = m_transients->device();
            const rojoRHI::SizeAlign footprint =
                resource.kind == ResourceKind::Texture
                    ? device.textureSizeAlign(textureDescOf(resource))
                    : device.bufferSizeAlign(bufferDescOf(resource));
            entry.size = footprint.size;
            entry.alignment = footprint.alignment;
            klass = aliasClassOf(resource.kind == ResourceKind::Texture, resource.format,
                                 resource.textureDesc, resource.bufferDesc, footprint);
        }
        plan.transients.push_back(entry);
        classes.push_back(klass);
    }

    // First fit, walked in lifetime order so a placement's occupants are compared against a
    // candidate that starts no earlier than any of them: one high-water mark per placement is then
    // all the overlap test needs. Ties in lifetime start are broken by declaration order, which is
    // what makes the layout a function of the declarations alone.
    std::vector<uint32_t> order;
    for (uint32_t index = 0; index < plan.transients.size(); ++index) {
        if (plan.transients[index].used) {
            order.push_back(index);
        }
    }
    std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        return plan.transients[a].firstPosition < plan.transients[b].firstPosition;
    });

    struct Placement {
        uint64_t offset = 0;
        AliasClass klass;
        // The last schedule position any occupant of these bytes reaches, and the occupant that
        // reaches it -- the resource a newcomer's reuse barrier has to order against.
        uint32_t lastPosition = 0;
        uint32_t lastOccupant = 0;
    };
    std::vector<Placement> placements;
    uint64_t highWater = 0;

    for (const uint32_t index : order) {
        TransientPlan& entry = plan.transients[index];
        plan.memory.requested += entry.size;

        bool reused = false;
        if (m_poolingEnabled) {
            for (Placement& placement : placements) {
                if (!(placement.klass == classes[index]) ||
                    placement.lastPosition >= entry.firstPosition) {
                    continue;
                }
                entry.offset = placement.offset;
                entry.aliases = true;
                entry.aliasedFrom = placement.lastOccupant;
                placement.lastPosition = entry.lastPosition;
                placement.lastOccupant = entry.resource;
                reused = true;
                break;
            }
        }
        if (!reused) {
            entry.offset = lmx::alignUpMultiple(highWater, entry.alignment);
            highWater = entry.offset + entry.size;
            placements.push_back({.offset = entry.offset,
                                  .klass = classes[index],
                                  .lastPosition = entry.lastPosition,
                                  .lastOccupant = entry.resource});
        }
    }

    plan.memory.highWater = highWater;
    plan.memory.aliasSavings =
        plan.memory.requested > highWater ? plan.memory.requested - highWater : 0;
    return plan;
}

} // namespace lmx::render
