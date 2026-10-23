//----------------------------------------------------------------------------------------------------------------------
/// @file ResourceUse.h
/// @brief Declares the render graph's own vocabulary for how a pass uses a texture or a buffer.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <string_view>

namespace lmx::render {

/// How a pass or an import uses a texture. The graph's records, its dump and the editor inspector
/// name uses with this vocabulary; it is converted to the RHI's only where commands are recorded.
enum class TextureUse {
    RenderTarget,    ///< Written as a render-pass attachment.
    ShaderRead,      ///< Read by a shader.
    StorageRead,     ///< Read through a storage binding.
    StorageWrite,    ///< Written through a storage binding.
    CopySource,      ///< Read by a copy command.
    CopyDestination, ///< Written by a copy command.
    ExternalRead,    ///< Read by an opaque RHI operation.
    ExternalWrite    ///< Written by an opaque RHI operation.
};

/// How a pass or an import uses a buffer.
enum class BufferUse {
    ShaderRead,      ///< Read by a shader.
    StorageRead,     ///< Read through a storage binding.
    StorageWrite,    ///< Written through a storage binding.
    CopySource,      ///< Read by a copy command.
    CopyDestination, ///< Written by a copy command.
    IndirectArgument ///< Read as indirect draw or dispatch arguments.
};

/// Returns the name a graph dump and the inspector print for @p use.
/// @param use The texture use to name.
/// @return The enumerator's spelling, such as "CopyDestination".
std::string_view textureUseName(TextureUse use);

/// Returns the name a graph dump and the inspector print for @p use.
/// @param use The buffer use to name.
/// @return The enumerator's spelling, such as "IndirectArgument".
std::string_view bufferUseName(BufferUse use);

} // namespace lmx::render
