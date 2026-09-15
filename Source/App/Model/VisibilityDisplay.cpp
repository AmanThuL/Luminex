//----------------------------------------------------------------------------------------------------------------------
/// @file VisibilityDisplay.cpp
/// @brief Formats visibility diagnostics and validates retained candidate identities.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Model/VisibilityDisplay.h"
#include "App/Model/VisibilityDiagnostics.h"

#include <algorithm>
#include <format>
#include <unordered_map>

namespace lmx::app {
//======================================================================================================================
std::string_view visibilityStateName(render::VisibilityState state) {
    switch (state) {
    case render::VisibilityState::Visible:
        return "Visible";
    case render::VisibilityState::Rejected:
        return "Rejected";
    case render::VisibilityState::Bypassed:
        return "Bypassed";
    }
    return "Unknown";
}
//======================================================================================================================
std::string_view visibilityBadge(render::VisibilityState state) {
    switch (state) {
    case render::VisibilityState::Visible:
        return "[V]";
    case render::VisibilityState::Rejected:
        return "[R]";
    case render::VisibilityState::Bypassed:
        return "[B]";
    }
    return "[?]";
}
//======================================================================================================================
std::string_view visibilityReasonName(render::VisibilityReason reason) {
    switch (reason) {
    case render::VisibilityReason::None:
        return "Frustum test";
    case render::VisibilityReason::Disabled:
        return "Culling disabled";
    case render::VisibilityReason::ViewUnculled:
        return "Shadow view unculled";
    case render::VisibilityReason::UnreliableBounds:
        return "Unreliable bounds";
    case render::VisibilityReason::NonFiniteTransform:
        return "Nonfinite transform";
    }
    return "Unknown";
}
//======================================================================================================================
std::vector<VisibilityField> visibilityFields(const render::VisibilityStatus& status,
                                              std::span<const rhi::PassTiming> timings) {
    std::vector<VisibilityField> fields;
    fields.push_back({"Classifier", std::string(classifyModeName(status.classifyMode))});
    fields.push_back({status.isRetired ? "Retired frame" : "Declared frame",
                      std::to_string(status.frameNumber)});
    if (status.classifyMode == render::ClassifyMode::Gpu && !status.isRetired) {
        fields.push_back({"GPU classification", "Awaiting retired frame"});
        return fields;
    }
    const auto append = [&fields](std::string_view name, const render::VisibilityResult& result) {
        fields.push_back({std::format("{} candidates / visible / rejected", name),
                          std::format("{} / {} / {}", result.candidates.size(), result.visible,
                                      result.rejected)});
        for (size_t i = 1; i < result.bypassed.size(); ++i) {
            fields.push_back(
                {std::format("{}: {}", name,
                             visibilityReasonName(static_cast<render::VisibilityReason>(i))),
                 std::to_string(result.bypassed[i])});
        }
    };
    append("Scene", status.scene);
    append("Shadow", status.shadow);
    const auto& s = status.submission;
    fields.push_back(
        {"Scene / shadow commands", std::format("{} / {}", s.sceneCommands, s.shadowCommands)});
    fields.push_back({"Instanced runs", std::to_string(s.instancedRuns)});
    fields.push_back(
        {"Valid row / argument payload", std::format("{} / {} B", s.listBytes, s.argumentBytes)});
    fields.push_back({"List / argument allocation",
                      std::format("{} / {} B", s.allocatedListBytes, s.allocatedArgumentBytes)});
    fields.push_back({"CPU classify / prepare",
                      std::format("{:.3f} / {:.3f} ms", status.classifyMs, status.prepareMs)});
    fields.push_back({"Candidate / run / chunk preparation",
                      std::format("{} / {} / {} B", s.candidateBytes, s.runBytes, s.chunkBytes)});
    fields.push_back(
        {"State / counter storage", std::format("{} / {} B", s.stateBytes, s.counterBytes)});
    fields.push_back({"Overflow", status.overflow ? "Work dropped" : "None"});
    if (status.classifyMode == render::ClassifyMode::Gpu) {
        const auto counters = [&fields](std::string_view name,
                                        const render::VisibilityCounters& c) {
            fields.push_back({std::format("{} emitted rows / commands", name),
                              std::format("{} / {}", c.emittedRows, c.emittedCommands)});
            fields.push_back({std::format("{} overflow rows / commands", name),
                              std::format("{} / {}", c.overflowedRows, c.overflowedCommands)});
        };
        counters("Scene", status.sceneCounters);
        counters("Shadow", status.shadowCounters);
        fields.push_back({"CPU oracle check", !status.checkEnabled   ? "Off"
                                              : status.checkPassed() ? "Passed"
                                                                     : "FAILED"});
        if (status.checkEnabled)
            fields.push_back(
                {"State / row / argument / counter mismatches",
                 std::format("{} / {} / {} / {}", status.stateMismatches, status.rowMismatches,
                             status.argumentMismatches, status.counterMismatches)});
        bool hasTiming = false;
        for (const auto& timing : timings) {
            if (timing.label.starts_with("lmx.pass.visibility.")) {
                fields.push_back({timing.label, std::format("{:.3f} ms", timing.gpuMilliseconds)});
                hasTiming = true;
            }
        }
        if (!hasTiming)
            fields.push_back({"GPU visibility timings", "Awaiting matching frame"});
    }
    return fields;
}
//======================================================================================================================
std::vector<VisibilityField> objectVisibilityFields(const render::InstanceVisibility* visibility) {
    if (visibility == nullptr)
        return {{"Visibility", "Awaiting this object's rendered frame"}};
    const auto& b = visibility->worldBounds;
    return {{"Visibility", std::string(visibilityStateName(visibility->state))},
            {"Reason", visibility->state == render::VisibilityState::Rejected
                           ? "Outside camera frustum"
                           : std::string(visibilityReasonName(visibility->reason))},
            {"World minimum",
             std::format("{:.3f}, {:.3f}, {:.3f}", b.minimum.x, b.minimum.y, b.minimum.z)},
            {"World maximum",
             std::format("{:.3f}, {:.3f}, {:.3f}", b.maximum.x, b.maximum.y, b.maximum.z)}};
}
//======================================================================================================================
void VisibilityDisplay::observe(const scene::Scene& scene, const render::VisibilityStatus& status) {
    if (m_generation != status.sceneGeneration)
        clear();
    m_generation = status.sceneGeneration;
    Snapshot snapshot{.frame = status.frameNumber, .generation = status.sceneGeneration};
    size_t size = 0;
    for (const auto& object : scene.objects)
        size = std::max(size, size_t{object.id.slot} + 1);
    snapshot.entries.assign(size, {});
    std::unordered_map<uint32_t, size_t> candidates;
    candidates.reserve(status.scene.candidates.size());
    for (size_t i = 0; i < status.scene.candidates.size(); ++i)
        candidates.emplace(status.scene.candidates[i].instanceRow, i);
    for (const auto& object : scene.objects) {
        const auto found = candidates.find(object.id.slot);
        if (found != candidates.end())
            snapshot.entries[object.id.slot] = {object.id, found->second};
    }
    if (status.classifyMode == render::ClassifyMode::Cpu) {
        m_entries = std::move(snapshot.entries);
        m_frame = status.frameNumber;
        m_status = status;
        m_pending.clear();
    } else {
        m_pending.push_back(std::move(snapshot));
        if (m_pending.size() > 8)
            m_pending.pop_front();
        if (m_status.classifyMode != render::ClassifyMode::Gpu || !m_status.isRetired) {
            m_status = status;
            m_entries.clear();
            m_frame = status.frameNumber;
        }
    }
}
//======================================================================================================================
void VisibilityDisplay::retire(const render::VisibilityStatus& status) {
    if (!status.isRetired || status.sceneGeneration != m_generation ||
        (m_status.isRetired && status.frameNumber <= m_frame))
        return;
    const auto found = std::ranges::find_if(m_pending, [&](const auto& snapshot) {
        return snapshot.frame == status.frameNumber &&
               snapshot.generation == status.sceneGeneration;
    });
    if (found == m_pending.end())
        return;
    m_frame = status.frameNumber;
    m_status = status;
    m_entries = found->entries;
    m_pending.erase(m_pending.begin(), std::next(found));
}
//======================================================================================================================
void VisibilityDisplay::observeTimings(uint64_t frame, std::span<const rhi::PassTiming> timings) {
    if (timings.empty() || (!m_timings.empty() && m_timings.back().frame >= frame))
        return;
    m_timings.push_back({.frame = frame, .passes = {timings.begin(), timings.end()}});
    if (m_timings.size() > 8)
        m_timings.pop_front();
}
//======================================================================================================================
std::span<const rhi::PassTiming> VisibilityDisplay::timings() const {
    const auto found = std::ranges::find_if(
        m_timings, [&](const auto& timing) { return timing.frame == m_status.frameNumber; });
    return found == m_timings.end() ? std::span<const rhi::PassTiming>{} : found->passes;
}
//======================================================================================================================
std::vector<VisibilityField> VisibilityDisplay::objectFields(scene::InstanceId id,
                                                             uint64_t sceneGeneration) const {
    const auto* candidate = find(id, m_status, sceneGeneration);
    auto fields = objectVisibilityFields(candidate);
    if (candidate != nullptr) {
        fields.insert(fields.begin(),
                      {m_status.classifyMode == render::ClassifyMode::Gpu ? "Retired frame"
                                                                          : "Declared frame",
                       std::to_string(m_status.frameNumber)});
    }
    return fields;
}
//======================================================================================================================
void VisibilityDisplay::clear() {
    m_entries.clear();
    m_pending.clear();
    m_timings.clear();
    m_status = {};
    m_frame = m_generation = 0;
}
//======================================================================================================================
const render::InstanceVisibility* VisibilityDisplay::find(scene::InstanceId id,
                                                          const render::VisibilityStatus& status,
                                                          uint64_t sceneGeneration) const {
    if ((status.classifyMode == render::ClassifyMode::Gpu && !status.isRetired) || id.store == 0 ||
        id.slot >= m_entries.size() || m_frame != status.frameNumber ||
        m_generation != status.sceneGeneration || m_generation != sceneGeneration)
        return nullptr;
    const auto& entry = m_entries[id.slot];
    if (entry.id != id || entry.candidate >= status.scene.candidates.size())
        return nullptr;
    return &status.scene.candidates[entry.candidate];
}
} // namespace lmx::app
