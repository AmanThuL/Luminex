//----------------------------------------------------------------------------------------------------------------------
/// @file CaptureSchema.cpp
/// @brief Forwards the capture-schema vocabulary to the RojoRHI v1 collector.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Gpu/CaptureSchema.h"

#include <rojoRHI/CaptureSchema.h>

#include <utility>

namespace lmx::engine {

//======================================================================================================================
void registerUniformStruct(SchemaUniformStruct layout) {
    rojoRHI::debug::SchemaUniformStruct converted;
    converted.name = std::move(layout.name);
    converted.slot = layout.slot;
    converted.sizeBytes = layout.sizeBytes;
    converted.fields.reserve(layout.fields.size());
    for (SchemaUniformField& field : layout.fields) {
        converted.fields.push_back(
            {std::move(field.name), field.offsetBytes, std::move(field.type)});
    }
    rojoRHI::debug::CaptureSchema::instance().registerUniformStruct(std::move(converted));
}

//======================================================================================================================
void setCaptureContext(SchemaContext context) {
    rojoRHI::debug::SchemaContext converted;
    converted.sceneName = std::move(context.sceneName);
    converted.frameIndex = context.frameIndex;
    converted.cameraPos = context.cameraPos;
    converted.boundingSphere = context.boundingSphere;
    converted.light0Direction = context.light0Direction;
    converted.light0Strength = context.light0Strength;
    converted.shadowFilter = std::move(context.shadowFilter);
    rojoRHI::debug::CaptureSchema::instance().setContext(std::move(converted));
}

} // namespace lmx::engine
