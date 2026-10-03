//----------------------------------------------------------------------------------------------------------------------
/// @file SceneDocumentContent.h
/// @brief Declares ordered instantiation of immutable document geometry, materials and animation.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Scene/SceneInstantiate.h"

#include <span>
#include <vector>

namespace lmx::engine {

/// Retains one document's uploaded resources while its mesh nodes append to the same scene.
/// Use a fresh binding for each document and scene; append each mesh node at most once.
struct DocumentContentBinding {
    std::vector<uint32_t> objectOfNode; ///< Document node to object index, or kGeneratedNode.

private:
    std::vector<MeshId> m_meshes;
    std::vector<MaterialId> m_materials;
    std::vector<bool> m_enabled;
    bool m_uploaded = false;

    friend asset::AssetResult<void> appendDocumentContent(rojoRHI::Device&, Scene&,
                                                          const asset::SceneDocument&,
                                                          std::span<const uint32_t>,
                                                          DocumentContentBinding&);
};

/// Uploads each geometry, image and material once, then appends meshNodes in the supplied order.
/// The document must have passed reader validation and prepareSceneDocument; binding is retained
/// across calls for interleaved assets/generators. Poses and keys keep their authored components.
asset::AssetResult<void> appendDocumentContent(rojoRHI::Device& device, Scene& scene,
                                               const asset::SceneDocument& document,
                                               std::span<const uint32_t> meshNodes,
                                               DocumentContentBinding& binding);

} // namespace lmx::engine
