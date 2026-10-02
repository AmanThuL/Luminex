//----------------------------------------------------------------------------------------------------------------------
/// @file SceneDocumentWriteInternal.h
/// @brief Declares private content serialization helpers shared by the document writer and saver.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Asset/Document/SceneDocument.h"

#include <span>

namespace lmx {
class JsonWriter;
namespace asset::detail {

/// Derives the immutable geometry filename from the decoded animation buffer filename.
std::string geometryUri(std::string_view bufferUri);
/// Derives the immutable PNG filename; valid models have unique, safe image names.
std::string imageUri(std::string_view bufferUri, const DocImage& image);
/// Checks a safe image filename stem against preceding images, with case-folded uniqueness.
AssetResult<void> validateImageName(std::string_view name, size_t index,
                                    std::span<const DocImage> previous);
/// Checks content indices, finite factors, filenames and immutable model hashes before writing.
AssetResult<void> validateDocumentContent(const SceneDocument& doc);
/// Appends geometry buffer views after the animation views in the open JSON array.
void geometryViews(JsonWriter& writer, const SceneDocument& doc);
/// Appends geometry accessors after animation accessors in the open JSON array.
void geometryAccessors(JsonWriter& writer, const SceneDocument& doc, size_t animationViews);
/// Writes mesh/material/image arrays using geometry accessors after animation accessors.
void contentJson(JsonWriter& writer, const SceneDocument& doc, std::string_view bufferUri,
                 size_t animationAccessors);
/// Writes authored bounds and immutable hashes inside the root LMX_scene object.
void contentExtensionJson(JsonWriter& writer, const SceneDocument& doc, std::string_view bufferUri);

} // namespace asset::detail
} // namespace lmx
