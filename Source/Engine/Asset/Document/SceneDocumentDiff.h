//----------------------------------------------------------------------------------------------------------------------
/// @file SceneDocumentDiff.h
/// @brief Declares canonical property changes between CPU scene documents.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Asset/Document/SceneDocument.h"

#include <cstdint>
#include <string>
#include <vector>

namespace lmx::asset {

/// Canonical document owner of a changed property.
enum class DocumentChangeOwner {
    Document,  ///< Top-level glTF or scene metadata.
    Node,      ///< Document node at index.
    Camera,    ///< Perspective camera at index.
    Light,     ///< Punctual light at index.
    Look,      ///< Saved scene look; index is zero.
    Animation, ///< Animation at index, including binary channel samples.
};

/// One changed canonical property, with empty text when a value is absent.
struct DocumentChange {
    DocumentChangeOwner owner; ///< Owner category.
    uint32_t index;            ///< Owner array index, or zero for Document and Look.
    std::string name;          ///< Current owner name, falling back to the previous name.
    std::string property;      ///< First canonical key below the owner; channel samples use a path.
    std::string before;        ///< Exact JSON value text before the change, or empty if absent.
    std::string after;         ///< Exact JSON value text after the change, or empty if absent.
};

/// Compares the canonical glTF JSON and external animation samples for two valid CPU documents.
/// Matches array owners by index and returns rows ordered by owner, index and property. The result
/// is empty exactly when the canonical JSON and companion buffer bytes are both equal.
std::vector<DocumentChange> diffSceneDocuments(const SceneDocument& before,
                                               const SceneDocument& after);

} // namespace lmx::asset
