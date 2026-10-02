//----------------------------------------------------------------------------------------------------------------------
/// @file SessionEdits.cpp
/// @brief Validates complete bridge-edit batches before applying persistent scene changes.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Session/SessionEdits.h"

#include "App/Model/Session/SessionQueries.h"
#include "Core/IO/JsonWriter.h"
#include "Core/Math/Aabb.h"
#include "Core/Math/Color.h"
#include "Engine/Asset/Document/Orientation.h"
#include "Engine/Lights/LocalLightMath.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace lmx::app {
namespace {

struct EnabledEdit {
    std::string subject;
    EditorSelection selection;
    bool enabled;
};

struct LightEdit {
    engine::LightId id;
    engine::LocalLight value;
};

struct PreparedBatch {
    std::map<size_t, DecomposedTransform> objects;
    std::map<uint64_t, LightEdit> lights;
    std::vector<EnabledEdit> enabled;
    std::unordered_map<std::string, bool> enabledBySubject;
    asset::SceneLook look;
    engine::SceneCamera camera;
    bool lookChanged = false;
    bool cameraChanged = false;
    std::vector<asset::DocumentChange> changes;
    std::vector<std::string> changedKeys;
};

//======================================================================================================================
std::string jsonVector(glm::vec3 value) {
    JsonWriter writer;
    writer.beginArray(true);
    writer.number(value.x);
    writer.number(value.y);
    writer.number(value.z);
    writer.endArray();
    return writer.take();
}

//======================================================================================================================
std::string jsonFloat(float value) {
    JsonWriter writer;
    writer.number(value);
    return writer.take();
}

//======================================================================================================================
std::string jsonBool(bool value) {
    JsonWriter writer;
    writer.boolean(value);
    return writer.take();
}

//======================================================================================================================
std::string jsonString(std::string_view value) {
    JsonWriter writer;
    writer.string(value);
    return writer.take();
}

//======================================================================================================================
std::string jsonExposure(const asset::SceneLook::Exposure& e) {
    JsonWriter writer;
    writer.beginObject();
    writer.key("ev");
    writer.number(e.ev);
    writer.key("autoEnabled");
    writer.boolean(e.autoEnabled);
    writer.key("lowPercentile");
    writer.number(e.lowPercentile);
    writer.key("highPercentile");
    writer.number(e.highPercentile);
    writer.key("targetGrey");
    writer.number(e.targetGrey);
    writer.key("evMin");
    writer.number(e.evMin);
    writer.key("evMax");
    writer.number(e.evMax);
    writer.key("compensationEv");
    writer.number(e.compensationEv);
    writer.key("adaptUpStopsPerSecond");
    writer.number(e.adaptUpStopsPerSecond);
    writer.key("adaptDownStopsPerSecond");
    writer.number(e.adaptDownStopsPerSecond);
    writer.endObject();
    return writer.take();
}

//======================================================================================================================
std::string jsonBloom(const asset::SceneLook::Bloom& bloom) {
    JsonWriter writer;
    writer.beginObject();
    writer.key("enabled");
    writer.boolean(bloom.enabled);
    writer.key("threshold");
    writer.number(bloom.threshold);
    writer.key("intensity");
    writer.number(bloom.intensity);
    writer.endObject();
    return writer.take();
}

//======================================================================================================================
std::string fieldValue(const SceneSession& session, const PreparedBatch& batch,
                       const SceneTreeRow& row, std::string_view field, bool after) {
    if (field == "enabled") {
        bool value = row.enabled;
        if (after) {
            const auto found = batch.enabledBySubject.find(sceneTreeSubjectId(row));
            if (found != batch.enabledBySubject.end())
                value = found->second;
        }
        return jsonBool(value);
    }
    if (row.subject == EditorSubject::Object) {
        const auto& object = session.scene().objects[row.index];
        const auto found = batch.objects.find(row.index);
        const auto value =
            after && found != batch.objects.end()
                ? found->second
                : DecomposedTransform{object.position, object.eulerDegrees, object.scale};
        return jsonVector(field == "position"       ? value.position
                          : field == "eulerDegrees" ? value.eulerDegrees
                                                    : value.scale);
    }
    if (row.subject == EditorSubject::LocalLight) {
        const auto found = batch.lights.find(engine::sceneLightKey(row.lightId));
        const auto light = after && found != batch.lights.end()
                               ? found->second.value
                               : *session.scene().light(row.lightId);
        if (field == "position")
            return jsonVector(light.position);
        if (field == "color")
            return jsonVector({linearToSrgb(light.colour.r), linearToSrgb(light.colour.g),
                               linearToSrgb(light.colour.b)});
        if (field == "intensity")
            return jsonFloat(light.intensity);
        if (field == "range")
            return jsonFloat(light.range);
        if (field == "direction")
            return jsonVector(light.direction);
        if (field == "innerCone")
            return jsonFloat(glm::degrees(light.innerCone));
        return jsonFloat(glm::degrees(light.outerCone));
    }
    if (row.subject == EditorSubject::Environment) {
        const auto& look = after ? batch.look : session.look();
        if (field == "exposure")
            return jsonExposure(look.exposure);
        if (field == "bloom")
            return jsonBloom(look.bloom);
        return jsonString(look.shadowFilter == asset::ShadowFilter::PCF ? "pcf" : "pcss");
    }
    const auto camera = after ? batch.camera : session.authoredSceneCamera();
    if (field == "position")
        return jsonVector(camera.position);
    return jsonFloat(field == "yaw" ? camera.yaw : camera.pitch);
}

//======================================================================================================================
std::expected<glm::vec3, std::string> vector3(const asset::JsonNode& value) {
    if (!value.isArray() || value.size() != 3)
        return std::unexpected("Expected a three-number array");
    glm::vec3 result;
    for (size_t i = 0; i < 3; ++i) {
        const auto part = value.at(i).asFloat();
        if (!part)
            return std::unexpected(part.error());
        result[static_cast<int>(i)] = *part;
    }
    return result;
}

//======================================================================================================================
std::expected<float, std::string> scalar(const asset::JsonNode& value) {
    auto number = value.asFloat();
    if (!number)
        return std::unexpected(number.error());
    return *number;
}

//======================================================================================================================
std::expected<void, std::string> updateExposure(asset::SceneLook::Exposure& exposure,
                                                const asset::JsonNode& value) {
    if (!value.isObject() || value.size() == 0 || value.size() > 10)
        return std::unexpected("exposure must be a nonempty object");
    std::unordered_set<std::string> seen;
    for (size_t i = 0; i < value.size(); ++i) {
        const auto name = value.memberName(i);
        if (!seen.insert(name).second)
            return std::unexpected("Duplicate exposure field " + name);
        const auto member = value.memberValue(i);
        if (name == "autoEnabled") {
            auto enabled = member.asBool();
            if (!enabled)
                return std::unexpected(enabled.error());
            exposure.autoEnabled = *enabled;
            continue;
        }
        auto number = scalar(member);
        if (!number)
            return std::unexpected(number.error());
        if (name == "ev")
            exposure.ev = *number;
        else if (name == "lowPercentile")
            exposure.lowPercentile = *number;
        else if (name == "highPercentile")
            exposure.highPercentile = *number;
        else if (name == "targetGrey")
            exposure.targetGrey = *number;
        else if (name == "evMin")
            exposure.evMin = *number;
        else if (name == "evMax")
            exposure.evMax = *number;
        else if (name == "compensationEv")
            exposure.compensationEv = *number;
        else if (name == "adaptUpStopsPerSecond")
            exposure.adaptUpStopsPerSecond = *number;
        else if (name == "adaptDownStopsPerSecond")
            exposure.adaptDownStopsPerSecond = *number;
        else
            return std::unexpected("Unknown exposure field " + name);
    }
    return {};
}

//======================================================================================================================
std::expected<void, std::string> updateBloom(asset::SceneLook::Bloom& bloom,
                                             const asset::JsonNode& value) {
    if (!value.isObject() || value.size() == 0 || value.size() > 3)
        return std::unexpected("bloom must be a nonempty object");
    std::unordered_set<std::string> seen;
    for (size_t i = 0; i < value.size(); ++i) {
        const auto name = value.memberName(i);
        if (!seen.insert(name).second)
            return std::unexpected("Duplicate bloom field " + name);
        const auto member = value.memberValue(i);
        if (name == "enabled") {
            auto enabled = member.asBool();
            if (!enabled)
                return std::unexpected(enabled.error());
            bloom.enabled = *enabled;
        } else {
            auto number = scalar(member);
            if (!number)
                return std::unexpected(number.error());
            if (name == "threshold")
                bloom.threshold = *number;
            else if (name == "intensity")
                bloom.intensity = *number;
            else
                return std::unexpected("Unknown bloom field " + name);
        }
    }
    return {};
}

//======================================================================================================================
std::expected<PreparedBatch, std::string> prepare(const SceneSession& session,
                                                  const SceneTreeView& tree,
                                                  std::span<const ProposalEdit> edits) {
    if (!session.loadedScene() || session.measurementActive())
        return std::unexpected("Scene edits require a document and no active measurement");
    if (edits.empty())
        return std::unexpected("edits must be nonempty");
    PreparedBatch batch;
    batch.look = session.look();
    batch.camera = session.authoredSceneCamera();
    const auto& binding = session.loadedScene()->binding;
    std::unordered_map<std::string, const SceneTreeRow*> rows;
    rows.reserve(tree.rows.size());
    for (const auto& row : tree.rows) {
        const auto id = sceneTreeSubjectId(row);
        if (!id.empty())
            rows.try_emplace(id, &row);
    }
    for (const auto& edit : edits) {
        const auto found = rows.find(edit.subject);
        if (found == rows.end() || found->second->generated)
            return std::unexpected("Unknown or generated subject " + edit.subject);
        const auto* row = found->second;
        const EditorSelection selection{.subject = row->subject,
                                        .index = row->index,
                                        .lightId = row->lightId,
                                        .node = row->node,
                                        .importedNode = row->importedNode};
        if (row->subject == EditorSubject::LocalLight && !session.scene().light(row->lightId))
            return std::unexpected("Stale local-light identity " + edit.subject);
        const auto parsed = asset::JsonTokens::parse(edit.value);
        if (!parsed)
            return std::unexpected("Invalid JSON value for " + edit.subject + "/" + edit.field);
        const auto value = parsed->root();
        const auto fail = [&] {
            return std::unexpected("Invalid field " + edit.subject + "/" + edit.field);
        };
        if (edit.field == "enabled") {
            if (edit.subject.starts_with("node:")) {
                if (row->node >= session.documentState().nodeEnabled.size())
                    return fail();
            } else if (edit.subject.starts_with("imported:")) {
                if (row->importedNode >= binding.importedNodes.size())
                    return fail();
            } else if (edit.subject.starts_with("object:")) {
                if (row->subject != EditorSubject::Object ||
                    row->index >= binding.objectImportedNode.size() ||
                    binding.objectImportedNode[row->index] == engine::kGeneratedNode)
                    return fail();
            } else if (edit.subject.starts_with("light:")) {
                if (row->subject != EditorSubject::LocalLight ||
                    !binding.lightNode.contains(engine::sceneLightKey(row->lightId)))
                    return fail();
            } else
                return fail();
            auto enabled = value.asBool();
            if (!enabled)
                return std::unexpected(enabled.error());
            batch.enabled.push_back({edit.subject, selection, *enabled});
            batch.enabledBySubject.insert_or_assign(edit.subject, *enabled);
        } else if (row->subject == EditorSubject::Object &&
                   (edit.field == "position" || edit.field == "eulerDegrees" ||
                    edit.field == "scale")) {
            if (row->index >= binding.objectImportedNode.size() ||
                !session.objectTransformPersistable(row->index))
                return fail();
            auto vector = vector3(value);
            if (!vector)
                return std::unexpected(vector.error());
            auto [it, inserted] = batch.objects.try_emplace(
                row->index, DecomposedTransform{session.scene().objects[row->index].position,
                                                session.scene().objects[row->index].eulerDegrees,
                                                session.scene().objects[row->index].scale});
            if (edit.field == "position")
                it->second.position = *vector;
            else if (edit.field == "eulerDegrees")
                it->second.eulerDegrees = *vector;
            else
                it->second.scale = *vector;
        } else if (row->subject == EditorSubject::LocalLight &&
                   (edit.field == "position" || edit.field == "color" ||
                    edit.field == "intensity" || edit.field == "range" ||
                    edit.field == "direction" || edit.field == "innerCone" ||
                    edit.field == "outerCone")) {
            const auto key = engine::sceneLightKey(row->lightId);
            const auto* current = session.scene().light(row->lightId);
            if (!current || !binding.lightNode.contains(key))
                return fail();
            auto initial = *current;
            initial.enabled = session.localLightEnabled(row->lightId);
            auto [it, inserted] = batch.lights.try_emplace(key, LightEdit{row->lightId, initial});
            auto& light = it->second.value;
            if (edit.field == "position" || edit.field == "color" || edit.field == "direction") {
                auto vector = vector3(value);
                if (!vector)
                    return std::unexpected(vector.error());
                if (edit.field == "position")
                    light.position = *vector;
                else if (edit.field == "color") {
                    if (glm::any(glm::lessThan(*vector, glm::vec3(0))) ||
                        glm::any(glm::greaterThan(*vector, glm::vec3(1))))
                        return fail();
                    light.colour = srgbToLinear(*vector);
                } else {
                    if (light.type != engine::LocalLightType::Spot || glm::length(*vector) <= 1e-5f)
                        return fail();
                    light.direction = glm::normalize(*vector);
                }
            } else {
                auto number = scalar(value);
                if (!number)
                    return std::unexpected(number.error());
                if (edit.field == "intensity")
                    light.intensity = *number;
                else if (edit.field == "range")
                    light.range = *number;
                else {
                    if (light.type != engine::LocalLightType::Spot)
                        return fail();
                    if (edit.field == "innerCone")
                        light.innerCone = glm::radians(*number);
                    else
                        light.outerCone = glm::radians(*number);
                }
            }
        } else if (edit.subject == "environment") {
            if (edit.field == "exposure") {
                auto result = updateExposure(batch.look.exposure, value);
                if (!result)
                    return std::unexpected(result.error());
            } else if (edit.field == "bloom") {
                auto result = updateBloom(batch.look.bloom, value);
                if (!result)
                    return std::unexpected(result.error());
            } else if (edit.field == "shadowFilter") {
                auto filter = value.asString();
                if (!filter || (*filter != "pcf" && *filter != "pcss"))
                    return fail();
                batch.look.shadowFilter =
                    *filter == "pcf" ? asset::ShadowFilter::PCF : asset::ShadowFilter::PCSS;
            } else
                return fail();
            batch.lookChanged = true;
        } else if (row->subject == EditorSubject::Camera &&
                   row->node == session.loadedScene()->document.camera) {
            if (edit.field == "position") {
                auto vector = vector3(value);
                if (!vector)
                    return std::unexpected(vector.error());
                batch.camera.position = *vector;
            } else if (edit.field == "yaw" || edit.field == "pitch") {
                auto radians = scalar(value);
                if (!radians)
                    return std::unexpected(radians.error());
                if (edit.field == "yaw")
                    batch.camera.yaw = *radians;
                else
                    batch.camera.pitch = *radians;
            } else
                return fail();
            batch.cameraChanged = true;
        } else
            return fail();
    }
    for (const auto& [index, value] : batch.objects) {
        if (!isFinite(value.position) || !isFinite(value.eulerDegrees) || !isFinite(value.scale) ||
            glm::any(glm::lessThan(value.scale, glm::vec3(0.01f))) ||
            glm::any(glm::greaterThan(value.scale, glm::vec3(100.0f))))
            return std::unexpected("Invalid object transform at index " + std::to_string(index));
    }
    for (const auto& [key, light] : batch.lights) {
        if (!engine::makeLightRow(light.value) ||
            glm::any(glm::lessThan(light.value.colour, glm::vec3(0))) ||
            glm::any(glm::greaterThan(light.value.colour, glm::vec3(1))) ||
            light.value.intensity < 0)
            return std::unexpected("Invalid combined local-light fields");
    }
    if (batch.cameraChanged &&
        (!isFinite(batch.camera.position) || !std::isfinite(batch.camera.yaw) ||
         !std::isfinite(batch.camera.pitch) ||
         std::abs(batch.camera.pitch) > glm::half_pi<float>() ||
         !std::isfinite(batch.camera.fovY) || batch.camera.fovY <= 0 ||
         batch.camera.fovY >= glm::pi<float>() || !std::isfinite(batch.camera.nearZ) ||
         batch.camera.nearZ <= 0 || !(batch.camera.farZ > batch.camera.nearZ)))
        return std::unexpected("Invalid combined authored camera pose");
    if (batch.cameraChanged)
        batch.camera.yaw = asset::unwrapYaw(0.0f, batch.camera.yaw);
    if (batch.lookChanged) {
        auto candidate = session.loadedScene()->document;
        candidate.look = batch.look;
        const auto valid = asset::validateSceneDocumentModel(candidate);
        if (!valid)
            return std::unexpected(valid.error().message);
    }
    std::unordered_set<std::string> seen;
    for (const auto& edit : edits) {
        if (!seen.insert(edit.subject + "/" + edit.field).second)
            continue;
        const auto* row = rows.at(edit.subject);
        auto owner = asset::DocumentChangeOwner::Node;
        uint32_t index = row->node == engine::kGeneratedNode ? 0 : row->node;
        if (row->subject == EditorSubject::Environment)
            owner = asset::DocumentChangeOwner::Look;
        else if (row->subject == EditorSubject::LocalLight && edit.field != "enabled") {
            owner = asset::DocumentChangeOwner::Light;
            index = *session.loadedScene()->document.nodes[index].light;
        }
        const auto before = fieldValue(session, batch, *row, edit.field, false);
        const auto after = fieldValue(session, batch, *row, edit.field, true);
        if (before != after) {
            batch.changes.push_back({owner, index, row->label, edit.field, before, after});
            batch.changedKeys.push_back(edit.subject + "/" + edit.field);
        }
    }
    return batch;
}

} // namespace

//======================================================================================================================
std::expected<std::vector<ProposalEdit>, std::string> parseEdits(const asset::JsonNode& args) {
    if (!args.isObject())
        return std::unexpected("Edit arguments must be an object");
    const auto edits = args.find("edits");
    if (!edits || !edits->isArray() || edits->size() == 0)
        return std::unexpected("edits must be a nonempty array");
    std::vector<ProposalEdit> result;
    for (const auto& entry : edits->elements()) {
        if (!entry.isObject() || entry.size() != 3)
            return std::unexpected("Each edit needs subject, field and value only");
        const auto subject = entry.find("subject");
        const auto field = entry.find("field");
        const auto value = entry.find("value");
        if (!subject || !field || !value)
            return std::unexpected("Each edit needs subject, field and value");
        const auto name = subject->asString();
        const auto property = field->asString();
        if (!name || name->empty() || !property || property->empty())
            return std::unexpected("Edit subject and field must be nonempty strings");
        result.push_back({*name, *property, std::string(value->sourceJson())});
    }
    return result;
}

//======================================================================================================================
std::expected<std::vector<asset::DocumentChange>, std::string>
previewEdits(const SceneSession& session, const SceneTreeView& tree,
             std::span<const ProposalEdit> edits) {
    auto prepared = prepare(session, tree, edits);
    if (!prepared)
        return std::unexpected(prepared.error());
    return std::move(prepared->changes);
}

//======================================================================================================================
std::expected<std::vector<std::string>, std::string>
changedEditKeys(const SceneSession& session, const SceneTreeView& tree,
                std::span<const ProposalEdit> edits) {
    auto prepared = prepare(session, tree, edits);
    if (!prepared)
        return std::unexpected(prepared.error());
    return std::move(prepared->changedKeys);
}

//======================================================================================================================
bool sessionEditNeedsCameraCut(const SceneTreeView& tree, std::span<const ProposalEdit> edits) {
    std::unordered_set<std::string> rendered;
    for (const auto& row : tree.rows)
        if (row.subject != EditorSubject::Camera && row.subject != EditorSubject::Environment)
            rendered.insert(sceneTreeSubjectId(row));
    for (const auto& edit : edits) {
        if (rendered.contains(edit.subject))
            return true;
    }
    return false;
}

//======================================================================================================================
rojoRHI::Result<void> applyEdits(SceneSession& session, const SceneTreeView& tree,
                                 std::span<const ProposalEdit> edits) {
    auto prepared = prepare(session, tree, edits);
    if (!prepared)
        return std::unexpected(rojoRHI::Error{rojoRHI::ErrorCode::InvalidDesc, prepared.error()});
    for (const auto& [index, transform] : prepared->objects)
        session.editObject(index, transform);
    for (const auto& [key, light] : prepared->lights)
        if (auto result = session.editLocalLight(light.id, light.value); !result)
            return result;
    if (prepared->lookChanged)
        session.editLook(prepared->look);
    if (prepared->cameraChanged)
        if (auto result = session.setSceneCamera(prepared->camera); !result)
            return result;
    for (const auto& edit : prepared->enabled) {
        rojoRHI::Result<void> result;
        if (edit.subject.starts_with("node:"))
            result = session.setNodeEnabled(edit.selection.node, edit.enabled);
        else if (edit.subject.starts_with("imported:"))
            result = session.setImportedNodeEnabled(edit.selection.importedNode, edit.enabled);
        else if (edit.subject.starts_with("object:"))
            result = session.setObjectEnabled(edit.selection.index, edit.enabled);
        else
            result = session.setLocalLightEnabled(edit.selection.lightId, edit.enabled);
        if (!result)
            return result;
    }
    return {};
}

//======================================================================================================================
void SessionAttribution::mark(std::string key, std::string client) {
    m_clients.insert_or_assign(std::move(key), std::move(client));
}

//======================================================================================================================
bool SessionAttribution::has(std::string_view key) const {
    return m_clients.contains(std::string(key));
}

//======================================================================================================================
std::string_view SessionAttribution::client(std::string_view key) const {
    const auto found = m_clients.find(std::string(key));
    return found == m_clients.end() ? std::string_view{} : std::string_view(found->second);
}

//======================================================================================================================
void SessionAttribution::clear() {
    m_clients.clear();
}

//======================================================================================================================
std::string sessionSubjectId(const EditorSelection& selection) {
    return sceneTreeSubjectId({.subject = selection.subject,
                               .index = selection.index,
                               .lightId = selection.lightId,
                               .node = selection.node,
                               .importedNode = selection.importedNode});
}

} // namespace lmx::app
