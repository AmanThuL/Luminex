//----------------------------------------------------------------------------------------------------------------------
/// @file VisibilityDisplay.cpp
/// @brief Formats visibility diagnostics and validates retained candidate identities.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Model/VisibilityDisplay.h"

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
std::vector<VisibilityField> visibilityFields(const render::VisibilityStatus& status) {
    std::vector<VisibilityField> fields;
    fields.push_back({"Declared frame", std::to_string(status.frameNumber)});
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
        {"List / argument payload", std::format("{} / {} B", s.listBytes, s.argumentBytes)});
    fields.push_back({"List / argument allocation",
                      std::format("{} / {} B", s.allocatedListBytes, s.allocatedArgumentBytes)});
    fields.push_back({"CPU classify / prepare",
                      std::format("{:.3f} / {:.3f} ms", status.classifyMs, status.prepareMs)});
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
    m_frame = status.frameNumber;
    m_generation = status.sceneGeneration;
    size_t size = 0;
    for (const auto& object : scene.objects)
        size = std::max(size, size_t{object.id.slot} + 1);
    m_entries.assign(size, {});
    std::unordered_map<uint32_t, size_t> candidates;
    candidates.reserve(status.scene.candidates.size());
    for (size_t i = 0; i < status.scene.candidates.size(); ++i)
        candidates.emplace(status.scene.candidates[i].instanceRow, i);
    for (const auto& object : scene.objects) {
        const auto found = candidates.find(object.id.slot);
        if (found != candidates.end())
            m_entries[object.id.slot] = {object.id, found->second};
    }
}
//======================================================================================================================
void VisibilityDisplay::clear() {
    m_entries.clear();
    m_frame = m_generation = 0;
}
//======================================================================================================================
const render::InstanceVisibility* VisibilityDisplay::find(scene::InstanceId id,
                                                          const render::VisibilityStatus& status,
                                                          uint64_t sceneGeneration) const {
    if (id.store == 0 || id.slot >= m_entries.size() || m_frame != status.frameNumber ||
        m_generation != status.sceneGeneration || m_generation != sceneGeneration)
        return nullptr;
    const auto& entry = m_entries[id.slot];
    if (entry.id != id || entry.candidate >= status.scene.candidates.size())
        return nullptr;
    return &status.scene.candidates[entry.candidate];
}
} // namespace lmx::app
