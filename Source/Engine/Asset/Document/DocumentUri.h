//----------------------------------------------------------------------------------------------------------------------
/// @file DocumentUri.h
/// @brief Declares private scene-document URI encoding and safe relative-path decoding.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Asset/Asset.h"

#include <string>
#include <string_view>

namespace lmx::asset::detail {

// The model owns decoded filesystem paths. Only the JSON boundary encodes or decodes them.
std::string encodeDocumentUri(std::string_view path);
AssetResult<std::string> decodeDocumentUri(std::string_view uri, std::string_view pointer);

} // namespace lmx::asset::detail
