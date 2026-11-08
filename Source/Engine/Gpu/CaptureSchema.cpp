//----------------------------------------------------------------------------------------------------------------------
/// @file CaptureSchema.cpp
/// @brief Serializes owned resource metadata and captured named root addresses.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Gpu/CaptureSchema.h"

#include "Core/Diagnostics/Assert.h"
#include "Core/Diagnostics/Log.h"
#include "Core/IO/Json.h"
#include "Engine/Gpu/FormatInfo.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <span>
#include <system_error>
#include <utility>

namespace lmx::engine {
namespace {

struct Resource {
    bool texture = false;
    uint64_t bits = 0;
    std::string label;
    rojoRHI::TextureType type = rojoRHI::TextureType::Tex2D;
    rojoRHI::Format format = rojoRHI::Format::Undefined;
    rojoRHI::Extent3D extent;
    uint32_t mips = 1;
    uint64_t size = 0;
};

struct FrameRoot {
    std::string name;
    rojoRHI::Addr addr;
};

struct Registry {
    std::vector<Resource> resources;
    std::vector<SchemaUniformStruct> layouts;
    std::vector<FrameRoot> roots;
    SchemaContext context;
    bool recording = false;
};

//======================================================================================================================
Registry& registry() {
    static Registry value;
    return value;
}

//======================================================================================================================
void registerResource(Resource resource) {
    if (resource.bits == 0) {
        return;
    }
    auto& resources = registry().resources;
    const auto found = std::ranges::find_if(resources, [&](const Resource& candidate) {
        return candidate.texture == resource.texture && candidate.bits == resource.bits;
    });
    if (found == resources.end()) {
        resources.push_back(std::move(resource));
    } else {
        *found = std::move(resource);
    }
}

//======================================================================================================================
void unregisterResource(bool texture, uint64_t bits) {
    std::erase_if(registry().resources, [&](const Resource& resource) {
        return resource.texture == texture && resource.bits == bits;
    });
}

//======================================================================================================================
void appendString(std::string& out, std::string_view value) {
    out += '"';
    appendJsonEscaped(out, value);
    out += '"';
}

//======================================================================================================================
void appendFloat(std::string& out, float value) {
    if (!std::isfinite(value)) {
        LMX_LOG_WARN("capture schema: non-finite float in the context, writing null");
        out += "null";
    } else {
        out += std::format("{}", value);
    }
}

//======================================================================================================================
void appendFloats(std::string& out, std::span<const float> values) {
    out += '[';
    for (size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            out += ", ";
        }
        appendFloat(out, values[index]);
    }
    out += ']';
}

//======================================================================================================================
std::string_view textureKind(rojoRHI::TextureType type) {
    using rojoRHI::TextureType;
    switch (type) {
    case TextureType::Tex1D:
        return "texture1d";
    case TextureType::Tex1DArray:
        return "texture1d-array";
    case TextureType::Tex2D:
        return "texture2d";
    case TextureType::Tex2DArray:
        return "texture2d-array";
    case TextureType::Tex3D:
        return "texture3d";
    case TextureType::Cube:
        return "cube";
    case TextureType::CubeArray:
        return "cube-array";
    }
    return "texture2d";
}

//======================================================================================================================
std::string renderSchema() {
    const Registry& data = registry();
    const SchemaContext& context = data.context;
    std::string out = "{\n  \"version\": 2,\n  \"context\": {\n    \"sceneName\": ";
    appendString(out, context.sceneName);
    out += std::format(",\n    \"frameIndex\": {},\n    \"cameraPos\": ", context.frameIndex);
    appendFloats(out, context.cameraPos);
    out += ",\n    \"boundingSphere\": ";
    appendFloats(out, context.boundingSphere);
    out += ",\n    \"light0Direction\": ";
    appendFloats(out, context.light0Direction);
    out += ",\n    \"light0Strength\": ";
    appendFloats(out, context.light0Strength);
    out += ",\n    \"shadowFilter\": ";
    appendString(out, context.shadowFilter);
    out += "\n  },\n  \"resources\": [";
    for (size_t index = 0; index < data.resources.size(); ++index) {
        const Resource& resource = data.resources[index];
        out += index == 0 ? "\n    {\"label\": " : ",\n    {\"label\": ";
        appendString(out, resource.label);
        out += ", \"kind\": ";
        appendString(out, resource.texture ? textureKind(resource.type) : "buffer");
        if (resource.texture) {
            out += std::format(
                ", \"format\": \"{}\", \"width\": {}, \"height\": {}, \"mipLevels\": {}",
                formatName(resource.format), resource.extent.width, resource.extent.height,
                resource.mips);
        } else {
            out += std::format(", \"sizeBytes\": {}", resource.size);
        }
        out += '}';
    }
    out += "\n  ],\n  \"uniformStructs\": [";
    for (size_t index = 0; index < data.layouts.size(); ++index) {
        const auto& layout = data.layouts[index];
        out += index == 0 ? "\n    {\"name\": " : ",\n    {\"name\": ";
        appendString(out, layout.name);
        out += std::format(", \"sizeBytes\": {}, \"fields\": [", layout.sizeBytes);
        for (size_t field = 0; field < layout.fields.size(); ++field) {
            const auto& value = layout.fields[field];
            out += field == 0 ? "\n      {\"name\": " : ",\n      {\"name\": ";
            appendString(out, value.name);
            out += std::format(", \"offsetBytes\": {}, \"type\": ", value.offsetBytes);
            appendString(out, value.type);
            out += '}';
        }
        out += "]}";
    }
    out += "\n  ],\n  \"frameRoots\": [";
    for (size_t index = 0; index < data.roots.size(); ++index) {
        const auto& root = data.roots[index];
        out += index == 0 ? "\n    {\"name\": " : ",\n    {\"name\": ";
        appendString(out, root.name);
        out += std::format(", \"addr\": {{\"lo\": {}, \"hi\": {}}}}}", root.addr.lo, root.addr.hi);
    }
    out += "\n  ]\n}\n";
    return out;
}

} // namespace

//======================================================================================================================
void registerUniformStruct(SchemaUniformStruct layout) {
    auto& layouts = registry().layouts;
    const auto found = std::ranges::find(layouts, layout.name, &SchemaUniformStruct::name);
    if (found == layouts.end()) {
        layouts.push_back(std::move(layout));
    } else {
        *found = std::move(layout);
    }
}

//======================================================================================================================
void setCaptureContext(SchemaContext context) {
    registry().context = std::move(context);
}

//======================================================================================================================
void registerCaptureResource(rojoRHI::Texture texture, const rojoRHI::TextureDesc& desc) {
    registerResource({.texture = true,
                      .bits = texture.bits,
                      .label = std::string(desc.label),
                      .type = desc.type,
                      .format = desc.format,
                      .extent = desc.extent,
                      .mips = desc.mips});
}

//======================================================================================================================
void registerCaptureResource(rojoRHI::Buffer buffer, const rojoRHI::BufferDesc& desc) {
    registerResource({.bits = buffer.bits, .label = std::string(desc.label), .size = desc.size});
}

//======================================================================================================================
void unregisterCaptureResource(rojoRHI::Texture texture) {
    unregisterResource(true, texture.bits);
}

//======================================================================================================================
void unregisterCaptureResource(rojoRHI::Buffer buffer) {
    unregisterResource(false, buffer.bits);
}

//======================================================================================================================
void beginCaptureFrameRecords() {
    Registry& data = registry();
    LMX_ASSERT(!data.recording, "beginCaptureFrameRecords: a record window is already open");
    data.roots.clear();
    data.context = {};
    data.recording = true;
}

//======================================================================================================================
void registerFrameRoot(std::string_view rootName, rojoRHI::Addr addr) {
    Registry& data = registry();
    if (!data.recording) {
        return;
    }
    const auto found = std::ranges::find_if(data.roots, [&](const FrameRoot& root) {
        return root.name == rootName && root.addr.lo == addr.lo && root.addr.hi == addr.hi;
    });
    if (found == data.roots.end()) {
        data.roots.push_back({std::string(rootName), addr});
    }
}

//======================================================================================================================
void endCaptureFrameRecords() {
    LMX_ASSERT(registry().recording, "endCaptureFrameRecords: no record window is open");
    registry().recording = false;
}

//======================================================================================================================
bool writeCaptureSchema(const std::filesystem::path& path) {
    const std::string text = renderSchema();
    std::filesystem::path temp = path;
    temp += ".tmp";
    std::error_code ignored;
    std::filesystem::remove(temp, ignored);
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    if (!file) {
        LMX_LOG_ERROR("capture schema: cannot open '{}' for writing", temp.string());
        return false;
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    if (!file) {
        LMX_LOG_ERROR("capture schema: failed while writing '{}'", temp.string());
        std::filesystem::remove(temp, ignored);
        return false;
    }
    std::error_code renameError;
    std::filesystem::rename(temp, path, renameError);
    if (renameError) {
        LMX_LOG_ERROR("capture schema: cannot move '{}' onto '{}': {}", temp.string(),
                      path.string(), renameError.message());
        std::filesystem::remove(temp, ignored);
        return false;
    }
    return true;
}

//======================================================================================================================
void resetCaptureSchemaForTest() {
    registry() = {};
}

} // namespace lmx::engine
