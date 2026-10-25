//----------------------------------------------------------------------------------------------------------------------
/// @file SceneStageLayouts.cpp
/// @brief Registers scene, sky and shared table layouts for GPU captures.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Passes/Scene/SceneStage.h"

#include "Engine/Geometry/Mesh.h"
#include "Engine/Gpu/CaptureSchema.h"
#include "Render/Passes/Scene/SceneRoots.h"
#include "Render/Passes/Scene/SceneStageInternal.h"

#include <cstddef>
#include <format>
#include <vector>

namespace lmx::render {
using namespace scene_detail;

//======================================================================================================================
void SceneStage::registerSceneTableLayoutsForCapture() {
    engine::registerUniformStruct({"VertexStorage",
                                   sizeof(engine::Vertex),
                                   {{"px", offsetof(engine::Vertex, px), "float"},
                                    {"py", offsetof(engine::Vertex, py), "float"},
                                    {"pz", offsetof(engine::Vertex, pz), "float"},
                                    {"nx", offsetof(engine::Vertex, nx), "float"},
                                    {"ny", offsetof(engine::Vertex, ny), "float"},
                                    {"nz", offsetof(engine::Vertex, nz), "float"},
                                    {"tx", offsetof(engine::Vertex, tx), "float"},
                                    {"ty", offsetof(engine::Vertex, ty), "float"},
                                    {"tz", offsetof(engine::Vertex, tz), "float"},
                                    {"tw", offsetof(engine::Vertex, tw), "float"},
                                    {"u", offsetof(engine::Vertex, u), "float"},
                                    {"v", offsetof(engine::Vertex, v), "float"}}});

    engine::registerUniformStruct(
        {.name = "InstanceRow",
         .sizeBytes = sizeof(engine::InstanceRow),
         .fields = {{"model", offsetof(engine::InstanceRow, model), "float4x4"},
                    {"previousModel", offsetof(engine::InstanceRow, previousModel), "float4x4"},
                    {"normalMatrix", offsetof(engine::InstanceRow, normalMatrix), "float4x4"},
                    {"meshRow", offsetof(engine::InstanceRow, meshRow), "uint"},
                    {"materialRow", offsetof(engine::InstanceRow, materialRow), "uint"},
                    {"flags", offsetof(engine::InstanceRow, flags), "uint"},
                    {"emissiveScale", offsetof(engine::InstanceRow, emissiveScale), "float"},
                    {"worldBoundsMin", offsetof(engine::InstanceRow, worldBoundsMin), "float3"},
                    {"worldBoundsMax", offsetof(engine::InstanceRow, worldBoundsMax), "float3"},
                    {"boundsPadding0", offsetof(engine::InstanceRow, boundsPadding0), "uint"},
                    {"boundsPadding1", offsetof(engine::InstanceRow, boundsPadding1), "uint"}}});
    engine::registerUniformStruct(
        {.name = "MaterialRow",
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
    engine::registerUniformStruct(
        {.name = "MeshRow",
         .sizeBytes = sizeof(engine::MeshRow),
         .fields = {{"firstIndex", offsetof(engine::MeshRow, firstIndex), "uint"},
                    {"indexCount", offsetof(engine::MeshRow, indexCount), "uint"},
                    {"firstVertex", offsetof(engine::MeshRow, firstVertex), "uint"},
                    {"vertexCount", offsetof(engine::MeshRow, vertexCount), "uint"},
                    {"boundsMin", offsetof(engine::MeshRow, boundsMin), "float3"},
                    {"boundsMax", offsetof(engine::MeshRow, boundsMax), "float3"},
                    {"boundsPadding0", offsetof(engine::MeshRow, boundsPadding0), "uint"},
                    {"boundsPadding1", offsetof(engine::MeshRow, boundsPadding1), "uint"}}});
    engine::registerUniformStruct(
        {.name = "LightRow",
         .sizeBytes = sizeof(engine::LightRow),
         .fields = {{"position", offsetof(engine::LightRow, position), "float3"},
                    {"range", offsetof(engine::LightRow, range), "float"},
                    {"strength", offsetof(engine::LightRow, strength), "float3"},
                    {"spotScale", offsetof(engine::LightRow, spotScale), "float"},
                    {"direction", offsetof(engine::LightRow, direction), "float3"},
                    {"spotOffset", offsetof(engine::LightRow, spotOffset), "float"},
                    {"boundCentre", offsetof(engine::LightRow, boundCentre), "float3"},
                    {"boundRadius", offsetof(engine::LightRow, boundRadius), "float"}}});
}

//======================================================================================================================
void SceneStage::registerPassLayoutsForCapture() {
    engine::registerUniformStruct(
        {"ScenePassRoot",
         sizeof(ScenePassRoot),
         {{"vertices", offsetof(ScenePassRoot, vertices), "Addr"},
          {"visibleRows", offsetof(ScenePassRoot, visibleRows), "Addr"},
          {"instances", offsetof(ScenePassRoot, instances), "Addr"},
          {"materials", offsetof(ScenePassRoot, materials), "Addr"},
          {"passUniforms", offsetof(ScenePassRoot, passUniforms), "Addr"},
          {"exposure", offsetof(ScenePassRoot, exposure), "Addr"},
          {"lights", offsetof(ScenePassRoot, lights), "Addr"},
          {"lightGrid", offsetof(ScenePassRoot, lightGrid), "Addr"},
          {"lightIndices", offsetof(ScenePassRoot, lightIndices), "Addr"},
          {"localLightParams", offsetof(ScenePassRoot, localLightParams), "Addr"},
          {"shadow", offsetof(ScenePassRoot, shadow), "uint"},
          {"irradiance", offsetof(ScenePassRoot, irradiance), "uint"},
          {"prefilteredEnv", offsetof(ScenePassRoot, prefilteredEnv), "uint"},
          {"dfg", offsetof(ScenePassRoot, dfg), "uint"},
          {"linearSampler", offsetof(ScenePassRoot, linearSampler), "uint"},
          {"shadowSampler", offsetof(ScenePassRoot, shadowSampler), "uint"},
          {"iblSampler", offsetof(ScenePassRoot, iblSampler), "uint"},
          {"padding", offsetof(ScenePassRoot, padding), "uint"}}});
    engine::registerUniformStruct({"SceneDrawRoot",
                                   sizeof(SceneDrawRoot),
                                   {{"pass", offsetof(SceneDrawRoot, pass), "Addr"},
                                    {"firstEntry", offsetof(SceneDrawRoot, firstEntry), "uint"},
                                    {"baseColor", offsetof(SceneDrawRoot, baseColor), "uint"},
                                    {"normal", offsetof(SceneDrawRoot, normal), "uint"},
                                    {"metalRough", offsetof(SceneDrawRoot, metalRough), "uint"},
                                    {"occlusion", offsetof(SceneDrawRoot, occlusion), "uint"},
                                    {"emissive", offsetof(SceneDrawRoot, emissive), "uint"},
                                    {"indirect", offsetof(SceneDrawRoot, indirect), "uint"},
                                    {"padding0", offsetof(SceneDrawRoot, padding0), "uint"},
                                    {"padding1", offsetof(SceneDrawRoot, padding1), "uint"},
                                    {"padding2", offsetof(SceneDrawRoot, padding2), "uint"}}});
    engine::registerUniformStruct({"SkyRoot",
                                   sizeof(SkyRoot),
                                   {{"vertices", offsetof(SkyRoot, vertices), "Addr"},
                                    {"uniforms", offsetof(SkyRoot, uniforms), "Addr"},
                                    {"exposure", offsetof(SkyRoot, exposure), "Addr"},
                                    {"sky", offsetof(SkyRoot, sky), "uint"},
                                    {"sampler", offsetof(SkyRoot, sampler), "uint"}}});
    engine::registerUniformStruct(
        {"DirLightUniform",
         sizeof(DirLightUniform),
         {{"strength", offsetof(DirLightUniform, strength), "float3"},
          {"strengthPadding", offsetof(DirLightUniform, strengthPadding), "float"},
          {"direction", offsetof(DirLightUniform, direction), "float3"},
          {"directionPadding", offsetof(DirLightUniform, directionPadding), "float"}}});
    // Derive array offsets from the element stride to avoid duplicated layout literals.
    constexpr uint32_t kLightCount = sizeof(PassUniforms::lights) / sizeof(DirLightUniform);
    std::vector<engine::SchemaUniformField> passFields{
        {"viewProj", offsetof(PassUniforms, viewProj), "float4x4"},
        {"shadowTransform", offsetof(PassUniforms, shadowTransform), "float4x4"},
        {"eyePos", offsetof(PassUniforms, eyePos), "float3"},
        {"eyePadding", offsetof(PassUniforms, eyePadding), "float"},
        {"time", offsetof(PassUniforms, time), "float"},
        {"preExposure", offsetof(PassUniforms, preExposure), "float"},
        {"alignmentPadding", offsetof(PassUniforms, alignmentPadding), "float[2]"}};
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
    passFields.push_back({"shadowCaster", offsetof(PassUniforms, shadowCaster), "int"});
    passFields.push_back({"tailPadding", offsetof(PassUniforms, tailPadding), "int[2]"});
    passFields.push_back(
        {"viewProjUnjittered", offsetof(PassUniforms, viewProjUnjittered), "float4x4"});
    passFields.push_back({"previousViewProjUnjittered",
                          offsetof(PassUniforms, previousViewProjUnjittered), "float4x4"});
    engine::registerUniformStruct({.name = "PassUniforms",
                                   .sizeBytes = sizeof(PassUniforms),
                                   .fields = std::move(passFields)});

    engine::registerUniformStruct(
        {.name = "SkyUniforms",
         .sizeBytes = sizeof(SkyUniforms),
         .fields = {
             {"viewProj", offsetof(SkyUniforms, viewProj), "float4x4"},
             {"eyePos", offsetof(SkyUniforms, eyePos), "float3"},
             {"eyePadding", offsetof(SkyUniforms, eyePadding), "float"},
             {"preExposure", offsetof(SkyUniforms, preExposure), "float"},
             {"jitterNdcX", offsetof(SkyUniforms, jitterNdcX), "float"},
             {"jitterNdcY", offsetof(SkyUniforms, jitterNdcY), "float"},
             {"jitterPadding", offsetof(SkyUniforms, jitterPadding), "float"},
             {"previousViewProj", offsetof(SkyUniforms, previousViewProj), "float4x4"},
             {"previousEyePos", offsetof(SkyUniforms, previousEyePos), "float3"},
             {"previousEyePosPadding", offsetof(SkyUniforms, previousEyePosPadding), "float"}}});
}

} // namespace lmx::render
