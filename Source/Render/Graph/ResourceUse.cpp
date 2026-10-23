//----------------------------------------------------------------------------------------------------------------------
/// @file ResourceUse.cpp
/// @brief Implements the names of the render graph's resource uses.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Graph/ResourceUse.h"

namespace lmx::render {

//======================================================================================================================
std::string_view textureUseName(TextureUse use) {
    switch (use) {
    case TextureUse::RenderTarget:
        return "RenderTarget";
    case TextureUse::ShaderRead:
        return "ShaderRead";
    case TextureUse::StorageRead:
        return "StorageRead";
    case TextureUse::StorageWrite:
        return "StorageWrite";
    case TextureUse::CopySource:
        return "CopySource";
    case TextureUse::CopyDestination:
        return "CopyDestination";
    case TextureUse::ExternalRead:
        return "ExternalRead";
    case TextureUse::ExternalWrite:
        return "ExternalWrite";
    }
    return "ShaderRead";
}

//======================================================================================================================
std::string_view bufferUseName(BufferUse use) {
    switch (use) {
    case BufferUse::ShaderRead:
        return "ShaderRead";
    case BufferUse::StorageRead:
        return "StorageRead";
    case BufferUse::StorageWrite:
        return "StorageWrite";
    case BufferUse::CopySource:
        return "CopySource";
    case BufferUse::CopyDestination:
        return "CopyDestination";
    case BufferUse::IndirectArgument:
        return "IndirectArgument";
    }
    return "ShaderRead";
}

} // namespace lmx::render
