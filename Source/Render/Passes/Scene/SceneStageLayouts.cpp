//----------------------------------------------------------------------------------------------------------------------
/// @file SceneStageLayouts.cpp
/// @brief Registers scene, sky and shared table layouts for GPU captures.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Passes/Scene/SceneStage.h"

#include "Render/Passes/Scene/SceneStageInternal.h"
#include <rojoRHI/CaptureSchema.h>

#include <cstddef>
#include <format>
#include <vector>

namespace lmx::render {
using namespace scene_detail;

//======================================================================================================================
void SceneStage::registerSceneTableLayoutsForCapture() {
    using rojoRHI::debug::CaptureSchema;
    CaptureSchema& schema = CaptureSchema::instance();

    schema.registerUniformStruct(
        {.name = "DrawUniforms",
         .slot = engine::kDrawUniformsSlot,
         .sizeBytes = sizeof(engine::DrawUniforms),
         .fields = {{"firstEntry", offsetof(engine::DrawUniforms, firstEntry), "uint"}}});
    schema.registerUniformStruct(
        {.name = "InstanceRow",
         .slot = engine::kSceneInstancesSlot,
         .sizeBytes = sizeof(engine::InstanceRow),
         .fields = {{"model", offsetof(engine::InstanceRow, model), "float4x4"},
                    {"previousModel", offsetof(engine::InstanceRow, previousModel), "float4x4"},
                    {"normalMatrix", offsetof(engine::InstanceRow, normalMatrix), "float4x4"},
                    {"meshRow", offsetof(engine::InstanceRow, meshRow), "uint"},
                    {"materialRow", offsetof(engine::InstanceRow, materialRow), "uint"},
                    {"flags", offsetof(engine::InstanceRow, flags), "uint"},
                    {"emissiveScale", offsetof(engine::InstanceRow, emissiveScale), "float"},
                    {"worldBoundsMin", offsetof(engine::InstanceRow, worldBoundsMin), "float3"},
                    {"worldBoundsMax", offsetof(engine::InstanceRow, worldBoundsMax), "float3"}}});
    schema.registerUniformStruct(
        {.name = "MaterialRow",
         .slot = engine::kSceneMaterialsSlot,
         .sizeBytes = sizeof(engine::MaterialRow),
         .fields = {
             {"uvTransform", offsetof(engine::MaterialRow, uvTransform), "float4x4"},
             {"albedo", offsetof(engine::MaterialRow, albedo), "float4"},
             {"emissive", offsetof(engine::MaterialRow, emissive), "float3"},
             {"roughness", offsetof(engine::MaterialRow, roughness), "float"},
             {"metallic", offsetof(engine::MaterialRow, metallic), "float"},
             {"occlusionStrength", offsetof(engine::MaterialRow, occlusionStrength), "float"},
             {"alphaCutoff", offsetof(engine::MaterialRow, alphaCutoff), "float"},
             {"flags", offsetof(engine::MaterialRow, flags), "uint"}}});
    schema.registerUniformStruct(
        {.name = "MeshRow",
         .slot = engine::kSceneMeshesSlot,
         .sizeBytes = sizeof(engine::MeshRow),
         .fields = {{"firstIndex", offsetof(engine::MeshRow, firstIndex), "uint"},
                    {"indexCount", offsetof(engine::MeshRow, indexCount), "uint"},
                    {"firstVertex", offsetof(engine::MeshRow, firstVertex), "uint"},
                    {"vertexCount", offsetof(engine::MeshRow, vertexCount), "uint"},
                    {"boundsMin", offsetof(engine::MeshRow, boundsMin), "float3"},
                    {"boundsMax", offsetof(engine::MeshRow, boundsMax), "float3"}}});
    schema.registerUniformStruct(
        {.name = "LightRow",
         .slot = engine::kSceneLightsSlot,
         .sizeBytes = sizeof(engine::LightRow),
         .fields = {{"position", offsetof(engine::LightRow, position), "float3"},
                    {"range", offsetof(engine::LightRow, range), "float"},
                    {"strength", offsetof(engine::LightRow, strength), "float3"},
                    {"spotScale", offsetof(engine::LightRow, spotScale), "float"},
                    {"direction", offsetof(engine::LightRow, direction), "float3"},
                    {"spotOffset", offsetof(engine::LightRow, spotOffset), "float"},
                    {"boundCentre", offsetof(engine::LightRow, boundCentre), "float3"},
                    {"boundRadius", offsetof(engine::LightRow, boundRadius), "float"}}});
    schema.registerUniformStruct(
        {.name = "LocalLightParams",
         .slot = engine::kLocalLightParamsSlot,
         .sizeBytes = sizeof(LocalLightParams),
         .fields = {{"mode", offsetof(LocalLightParams, mode), "uint"},
                    {"rowCount", offsetof(LocalLightParams, rowCount), "uint"},
                    {"gridX", offsetof(LocalLightParams, gridX), "uint"},
                    {"gridY", offsetof(LocalLightParams, gridY), "uint"},
                    {"gridZ", offsetof(LocalLightParams, gridZ), "uint"},
                    {"activeOriginX", offsetof(LocalLightParams, activeOriginX), "uint"},
                    {"activeOriginY", offsetof(LocalLightParams, activeOriginY), "uint"},
                    {"activeWidth", offsetof(LocalLightParams, activeWidth), "uint"},
                    {"activeHeight", offsetof(LocalLightParams, activeHeight), "uint"},
                    {"sliceDepth", offsetof(LocalLightParams, sliceDepth), "float[25]"}}});
}

//======================================================================================================================
void SceneStage::registerPassLayoutsForCapture() {
    using rojoRHI::debug::CaptureSchema;
    CaptureSchema& schema = CaptureSchema::instance();

    using rojoRHI::debug::SchemaUniformField;

    // Derive array offsets from the element stride to avoid duplicated layout literals.
    constexpr uint32_t kLightCount = sizeof(PassUniforms::lights) / sizeof(DirLightUniform);
    std::vector<SchemaUniformField> passFields{
        {"viewProj", offsetof(PassUniforms, viewProj), "float4x4"},
        {"shadowTransform", offsetof(PassUniforms, shadowTransform), "float4x4"},
        {"eyePos", offsetof(PassUniforms, eyePos), "float3"},
        {"time", offsetof(PassUniforms, time), "float"},
        {"preExposure", offsetof(PassUniforms, preExposure), "float"}};
    for (uint32_t light = 0; light < kLightCount; ++light) {
        const uint32_t base =
            uint32_t{offsetof(PassUniforms, lights)} + light * uint32_t{sizeof(DirLightUniform)};
        passFields.push_back({std::format("lights[{}].strength", light),
                              base + uint32_t{offsetof(DirLightUniform, strength)}, "float3"});
        passFields.push_back({std::format("lights[{}].direction", light),
                              base + uint32_t{offsetof(DirLightUniform, direction)}, "float3"});
    }
    // Preserve offset order for comparison with raw capture bytes.
    passFields.push_back(
        {"shadowFilter", offsetof(PassUniforms, shadowFilter), "int"}); // kShadowFilterPcf/Pcss
    passFields.push_back(
        {"viewProjUnjittered", offsetof(PassUniforms, viewProjUnjittered), "float4x4"});
    passFields.push_back({"previousViewProjUnjittered",
                          offsetof(PassUniforms, previousViewProjUnjittered), "float4x4"});
    schema.registerUniformStruct({.name = "PassUniforms",
                                  .slot = kPassUniformsSlot,
                                  .sizeBytes = sizeof(PassUniforms),
                                  .fields = std::move(passFields)});

    schema.registerUniformStruct(
        {.name = "SkyUniforms",
         .slot = kPassUniformsSlot,
         .sizeBytes = sizeof(SkyUniforms),
         .fields = {{"viewProj", offsetof(SkyUniforms, viewProj), "float4x4"},
                    {"eyePos", offsetof(SkyUniforms, eyePos), "float3"},
                    {"preExposure", offsetof(SkyUniforms, preExposure), "float"},
                    {"jitterNdcX", offsetof(SkyUniforms, jitterNdcX), "float"},
                    {"jitterNdcY", offsetof(SkyUniforms, jitterNdcY), "float"},
                    {"previousViewProj", offsetof(SkyUniforms, previousViewProj), "float4x4"},
                    {"previousEyePos", offsetof(SkyUniforms, previousEyePos), "float3"}}});
}

} // namespace lmx::render
