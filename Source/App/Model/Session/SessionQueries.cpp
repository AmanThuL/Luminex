//----------------------------------------------------------------------------------------------------------------------
/// @file SessionQueries.cpp
/// @brief Encodes published editor data for read-only local session queries.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Session/SessionQueries.h"

#include "App/Model/Rendering/Lighting/LightingDiagnostics.h"
#include "App/Model/Rendering/Settings/RenderSettingCommands.h"
#include "App/Model/Rendering/Visibility/VisibilityDiagnostics.h"
#include "Core/IO/JsonWriter.h"

#include <array>
#include <format>
#include <string>
#include <utility>

namespace lmx::app {
namespace {

//======================================================================================================================
std::string subjectId(const SceneTreeRow& row) {
    if (row.importedNode != engine::kGeneratedNode)
        return std::format("imported:{}", row.importedNode);
    if (row.generated && row.subject == EditorSubject::LocalLight)
        return std::format("light:{}:{}", row.lightId.slot, row.lightId.generation);
    if (row.generated && row.subject == EditorSubject::Object)
        return std::format("object:{}", row.index);
    if (row.node != engine::kGeneratedNode)
        return std::format("node:{}", row.node);
    if (row.subject == EditorSubject::Environment)
        return "environment";
    if (row.subject == EditorSubject::Camera)
        return "camera";
    if (row.subject == EditorSubject::DirectionalLight)
        return std::format("dirlight:{}", row.index);
    if (row.subject == EditorSubject::LocalLight)
        return std::format("light:{}:{}", row.lightId.slot, row.lightId.generation);
    if (row.subject == EditorSubject::Object)
        return std::format("object:{}", row.index);
    return {};
}

//======================================================================================================================
std::string_view subjectKind(EditorSubject subject) {
    switch (subject) {
    case EditorSubject::None:
        return "none";
    case EditorSubject::Camera:
        return "camera";
    case EditorSubject::DirectionalLight:
        return "directional-light";
    case EditorSubject::LocalLight:
        return "local-light";
    case EditorSubject::Object:
        return "object";
    case EditorSubject::Group:
        return "group";
    case EditorSubject::Environment:
        return "environment";
    }
    return "none";
}

//======================================================================================================================
std::string_view actorName(Actor actor) {
    switch (actor) {
    case Actor::Operator:
        return "operator";
    case Actor::System:
        return "system";
    case Actor::Agent:
        return "Agent";
    }
    return "system";
}

//======================================================================================================================
void writeVector(JsonWriter& writer, const glm::vec3& vector) {
    writer.beginArray(true);
    writer.number(vector.x);
    writer.number(vector.y);
    writer.number(vector.z);
    writer.endArray();
}

} // namespace

//======================================================================================================================
std::string sceneTreeSubjectId(const SceneTreeRow& row) {
    return subjectId(row);
}

//======================================================================================================================
std::string hierarchyJson(const SceneTreeView& tree) {
    JsonWriter writer;
    writer.beginObject();
    writer.key("rows");
    writer.beginArray();
    for (const auto& row : tree.rows) {
        const auto id = subjectId(row);
        if (id.empty())
            continue;
        writer.beginObject();
        writer.key("id");
        writer.string(id);
        writer.key("label");
        writer.string(row.label);
        writer.key("kind");
        writer.string(subjectKind(row.subject));
        writer.key("depth");
        writer.integer(row.depth);
        writer.key("enabled");
        writer.boolean(row.enabled);
        writer.key("effective");
        writer.boolean(row.effective);
        writer.key("generated");
        writer.boolean(row.generated);
        writer.endObject();
    }
    writer.endArray();
    writer.endObject();
    return writer.take();
}

//======================================================================================================================
std::optional<EditorSelection> parseSubjectId(std::string_view id, const SceneTreeView& tree) {
    for (const auto& row : tree.rows) {
        if (!id.empty() && subjectId(row) == id)
            return EditorSelection{.subject = row.subject,
                                   .index = row.index,
                                   .lightId = row.lightId,
                                   .node = row.node,
                                   .importedNode = row.importedNode};
    }
    return std::nullopt;
}

//======================================================================================================================
std::string selectionJson(const EditorSelection& selection, const SceneTreeView& tree) {
    JsonWriter writer;
    writer.beginObject();
    writer.key("id");
    std::string id;
    for (const auto& row : tree.rows) {
        if (!subjectId(row).empty() && sceneTreeRowSelected(row, selection)) {
            id = subjectId(row);
            break;
        }
    }
    if (id.empty())
        writer.string(selection.subject == EditorSubject::Camera ? "camera" : "");
    else
        writer.string(id);
    writer.key("kind");
    writer.string(subjectKind(selection.subject));
    writer.endObject();
    return writer.take();
}

//======================================================================================================================
std::string cameraJson(const engine::Camera& camera) {
    JsonWriter writer;
    writer.beginObject();
    writer.key("position");
    writeVector(writer, camera.position);
    writer.key("yaw");
    writer.number(camera.yaw);
    writer.key("pitch");
    writer.number(camera.pitch);
    writer.key("fovY");
    writer.number(camera.fovY);
    writer.key("nearZ");
    writer.number(camera.nearZ);
    writer.key("farZ");
    writer.number(camera.farZ);
    writer.endObject();
    return writer.take();
}

//======================================================================================================================
std::string settingsJson(const EditorRenderSettings& settings) {
    JsonWriter writer;
    writer.beginObject();
    constexpr std::array keys{RenderSettingKey::Temporal,       RenderSettingKey::RenderScale,
                              RenderSettingKey::Visibility,     RenderSettingKey::Classify,
                              RenderSettingKey::ClassifyCheck,  RenderSettingKey::Occlusion,
                              RenderSettingKey::OcclusionCheck, RenderSettingKey::Submission,
                              RenderSettingKey::LocalLights,    RenderSettingKey::LightCheck};
    for (const auto key : keys) {
        writer.key(renderSettingName(key));
        writer.string(renderSettingValue(settings, key));
    }
    writer.endObject();
    return writer.take();
}

//======================================================================================================================
std::string performanceJson(const PerformanceSnapshot& snapshot) {
    JsonWriter writer;
    writer.beginObject();
    writer.key("frameId");
    writer.integer(snapshot.frameId);
    writer.key("waiting");
    writer.boolean(snapshot.waitingForSamples);
    writer.key("objects");
    writer.integer(snapshot.objectCount);
    writer.key("draws");
    writer.integer(snapshot.drawCount);
    writer.key("timedPassSumMilliseconds");
    writer.number(snapshot.timedPassSumMilliseconds);
    writer.key("passes");
    writer.beginArray();
    for (const auto& pass : snapshot.passRows) {
        writer.beginObject();
        writer.key("label");
        writer.string(pass.label);
        writer.key("averageMilliseconds");
        writer.number(pass.averageGpuMilliseconds);
        writer.key("latestMilliseconds");
        writer.number(pass.latestGpuMilliseconds);
        writer.key("minimumMilliseconds");
        writer.number(pass.minimumGpuMilliseconds);
        writer.key("maximumMilliseconds");
        writer.number(pass.maximumGpuMilliseconds);
        writer.key("samples");
        writer.integer(pass.sampleCount);
        writer.endObject();
    }
    writer.endArray();
    writer.endObject();
    return writer.take();
}

//======================================================================================================================
std::string consoleJson(const ConsoleSnapshot& snapshot, uint64_t afterSequence) {
    JsonWriter writer;
    writer.beginObject();
    writer.key("nextSequence");
    writer.integer(snapshot.nextSequence);
    writer.key("evictedEntries");
    writer.integer(snapshot.evictedEntries);
    writer.key("entries");
    writer.beginArray();
    for (const auto& entry : snapshot.entries) {
        if (entry.sequence <= afterSequence)
            continue;
        writer.beginObject();
        writer.key("sequence");
        writer.integer(entry.sequence);
        writer.key("timestampMilliseconds");
        writer.integer(entry.timestampMilliseconds);
        writer.key("severity");
        writer.string(consoleSeverityName(entry.severity));
        writer.key("actor");
        writer.string(actorName(entry.actor));
        writer.key("message");
        writer.string(entry.message);
        writer.key("truncated");
        writer.boolean(entry.truncated);
        writer.endObject();
    }
    writer.endArray();
    writer.endObject();
    return writer.take();
}

//======================================================================================================================
std::string proposalsJson(const ProposalQueue& proposals) {
    JsonWriter writer;
    writer.beginObject();
    writer.key("proposals");
    writer.beginArray();
    for (const auto& proposal : proposals.all()) {
        writer.beginObject();
        writer.key("id");
        writer.integer(proposal.id);
        writer.key("source");
        writer.string(proposal.source == ProposalSource::File ? "file" : "bridge");
        writer.key("state");
        writer.string(sessionStateLabel(proposal.state));
        writer.key("client");
        writer.string(proposal.client);
        writer.key("summary");
        writer.string(proposal.summary);
        writer.key("hash");
        writer.string(proposal.hash);
        writer.key("changes");
        writer.integer(proposal.changes.size());
        writer.endObject();
    }
    writer.endArray();
    writer.endObject();
    return writer.take();
}

//======================================================================================================================
std::string logJson(const SessionLog& log) {
    JsonWriter writer;
    writer.beginObject();
    writer.key("actions");
    writer.beginArray();
    for (const auto& action : log.actions()) {
        writer.beginObject();
        writer.key("sequence");
        writer.integer(action.sequence);
        writer.key("timestampMilliseconds");
        writer.integer(action.timestampMilliseconds);
        writer.key("actor");
        writer.string(actorName(action.actor));
        writer.key("client");
        writer.string(action.client);
        writer.key("command");
        writer.string(action.command);
        writer.key("arguments");
        writer.string(action.arguments);
        writer.key("tier");
        writer.integer(static_cast<uint8_t>(action.tier));
        writer.key("outcome");
        writer.string(action.outcome);
        writer.endObject();
    }
    writer.endArray();
    writer.endObject();
    return writer.take();
}

//======================================================================================================================
std::string statusJson(const SessionStatus& status) {
    JsonWriter writer;
    writer.beginObject();
    writer.key("document");
    writer.beginObject();
    writer.key("path");
    writer.string(status.documentPath);
    writer.key("hash");
    writer.string(status.documentHash);
    writer.key("dirty");
    writer.boolean(status.dirty);
    writer.endObject();
    writer.key("playback");
    writer.string(status.playback);
    writer.key("measuring");
    writer.boolean(status.measuring);
    writer.key("tier");
    writer.integer(static_cast<uint8_t>(status.tier));
    writer.key("pendingProposals");
    writer.integer(status.pendingProposals);
    writer.key("job");
    writer.string(status.job);
    writer.endObject();
    return writer.take();
}

//======================================================================================================================
std::string readingsJson(const render::VisibilityStatus& visibility,
                         const render::LightingStatus& lighting) {
    return std::format("{{\"visibility\":{},\"lighting\":{}}}",
                       visibilityDiagnosticsJson(visibility), lightingDiagnosticsJson(lighting));
}

//======================================================================================================================
SessionAction sessionTierAction(std::string client, SessionTier ceiling,
                                int64_t timestampMilliseconds) {
    return {.timestampMilliseconds = timestampMilliseconds,
            .actor = Actor::Operator,
            .client = std::move(client),
            .command = "session.tier",
            .arguments = std::to_string(static_cast<uint8_t>(ceiling)),
            .tier = ceiling,
            .outcome = "changed"};
}

} // namespace lmx::app
