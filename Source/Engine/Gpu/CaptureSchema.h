//----------------------------------------------------------------------------------------------------------------------
/// @file CaptureSchema.h
/// @brief Declares the capture-schema vocabulary Luminex code registers uniform layouts in.
//----------------------------------------------------------------------------------------------------------------------
/// Luminex code names only these types. On RojoRHI v1 they forward to the backend's capture schema
/// singleton, because the backend owns the resource and frame-data records and writes the sidecar
/// at `endCapture`. The switch to v2 replaces the bodies with a Luminex writer keyed by root-struct
/// name and address.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace lmx::engine {

/// Describes one named field in a captured uniform layout.
struct SchemaUniformField {
    std::string name;         ///< Field name as declared in the shader layout.
    uint32_t offsetBytes = 0; ///< Byte offset from the beginning of the uniform structure.
    std::string type;         ///< Shader scalar, vector, or matrix type name.
};

/// Describes a captured uniform structure and its argument-table slot.
struct SchemaUniformStruct {
    std::string name;                       ///< Stable uniform structure name.
    uint32_t slot = 0;                      ///< Argument-table buffer slot.
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

} // namespace lmx::engine
