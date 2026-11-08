//----------------------------------------------------------------------------------------------------------------------
/// @file CaptureSchema.h
/// @brief Declares the capture-schema vocabulary Luminex code registers uniform layouts in.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include <rojoRHI/RHI.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace lmx::engine {

/// Describes one named field in a captured uniform layout.
struct SchemaUniformField {
    std::string name;         ///< Field name as declared in the shader layout.
    uint32_t offsetBytes = 0; ///< Byte offset from the beginning of the uniform structure.
    std::string type;         ///< Shader scalar, vector, or matrix type name.
};

/// Describes a captured root structure by name and byte layout.
struct SchemaUniformStruct {
    std::string name;                       ///< Stable uniform structure name.
    uint32_t sizeBytes = 0;                 ///< Total structure size in bytes.
    std::vector<SchemaUniformField> fields; ///< Ordered field layout.
};

/// Captures scene, camera, light, and shadow-filter context for one frame.
struct SchemaContext {
    std::string sceneName;                  ///< Stable scene identifier.
    uint64_t frameIndex = 0;                ///< Captured application frame index.
    std::array<float, 3> cameraPos{};       ///< World-space camera position.
    std::array<float, 4> boundingSphere{};  ///< World-space xyz center and radius in w.
    std::array<float, 3> light0Direction{}; ///< Primary directional-light direction.
    std::array<float, 3> light0Strength{};  ///< Primary directional-light linear strength.
    std::string shadowFilter;               ///< Active shadow filter name, PCF or PCSS.
};

/// Registers one uniform layout for the capture sidecar; re-registering a name replaces it.
void registerUniformStruct(SchemaUniformStruct layout);

/// Sets the scene, camera and light context the sidecar records for the captured frame.
void setCaptureContext(SchemaContext context);

/// Registers owned texture metadata keyed by resource kind and handle bits; copies the label.
/// Calls are serialized on the renderer-owning thread for Luminex's single active device.
void registerCaptureResource(rojoRHI::Texture texture, const rojoRHI::TextureDesc& desc);
/// Registers owned buffer metadata keyed separately from texture handles; copies the label.
void registerCaptureResource(rojoRHI::Buffer buffer, const rojoRHI::BufferDesc& desc);
/// Removes a logically released texture; missing or null handles are harmless.
void unregisterCaptureResource(rojoRHI::Texture texture);
/// Removes a logically released buffer; missing or null handles are harmless.
void unregisterCaptureResource(rojoRHI::Buffer buffer);
/// Opens a capture record window, clearing roots and context while preserving resources/layouts.
/// No record window may already be open.
void beginCaptureFrameRecords();
/// Registers a named root address once per name/address pair; ignored outside a record window.
void registerFrameRoot(std::string_view rootName, rojoRHI::Addr addr);
/// Closes the open record window, retaining its roots and context for serialization.
void endCaptureFrameRecords();
/// Atomically replaces a sidecar with schema version two; returns false and logs I/O failure.
/// Does not mutate registry state. Nonfinite context numbers become JSON null.
bool writeCaptureSchema(const std::filesystem::path& path);
/// Clears test registry state; requires no live registered owners or active real capture.
void resetCaptureSchemaForTest();

} // namespace lmx::engine
