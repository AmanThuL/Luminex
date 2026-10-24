//----------------------------------------------------------------------------------------------------------------------
/// @file FormatInfo.cpp
/// @brief Implements stable format spellings and explicit block storage sizes.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Gpu/FormatInfo.h"

#include "Core/Diagnostics/Assert.h"

namespace lmx::engine {

//======================================================================================================================
uint32_t bytesPerTexel(rojoRHI::Format format) {
    LMX_ASSERT(blockExtent(format) == 1, "bytesPerTexel: compressed formats require bytesPerBlock");
    return bytesPerBlock(format);
}

//======================================================================================================================
uint32_t bytesPerBlock(rojoRHI::Format format) {
    using rojoRHI::Format;
    switch (format) {
    case Format::Undefined:
        return 0;
    case Format::R8Unorm:
        return 1;
    case Format::R16Float:
        return 2;
    case Format::BGRA8Unorm:
    case Format::RGBA8Unorm:
    case Format::RGBA8Unorm_sRGB:
    case Format::RG16Float:
    case Format::D32Float:
    case Format::R32Float:
    case Format::RGB10A2Unorm:
        return 4;
    case Format::RGBA16Float:
    case Format::BC1Unorm:
    case Format::BC1Unorm_sRGB:
    case Format::D32FloatS8:
        return 8;
    }
    return 0;
}

//======================================================================================================================
uint32_t blockExtent(rojoRHI::Format format) {
    return format == rojoRHI::Format::BC1Unorm || format == rojoRHI::Format::BC1Unorm_sRGB ? 4 : 1;
}

//======================================================================================================================
bool isDepthFormat(rojoRHI::Format format) {
    return format == rojoRHI::Format::D32Float || format == rojoRHI::Format::D32FloatS8;
}

//======================================================================================================================
std::string_view formatName(rojoRHI::Format format) {
    using rojoRHI::Format;
    switch (format) {
    case Format::Undefined:
        return "Unknown";
    case Format::BGRA8Unorm:
        return "BGRA8Unorm";
    case Format::RGBA8Unorm:
        return "RGBA8Unorm";
    case Format::RGBA8Unorm_sRGB:
        return "RGBA8Unorm_sRGB";
    case Format::RGBA16Float:
        return "RGBA16Float";
    case Format::RG16Float:
        return "RG16Float";
    case Format::R8Unorm:
        return "R8Unorm";
    case Format::BC1Unorm:
        return "BC1Unorm";
    case Format::BC1Unorm_sRGB:
        return "BC1Unorm_sRGB";
    case Format::D32Float:
        return "D32Float";
    case Format::R16Float:
        return "R16Float";
    case Format::R32Float:
        return "R32Float";
    case Format::D32FloatS8:
        return "D32FloatS8";
    case Format::RGB10A2Unorm:
        return "RGB10A2Unorm";
    }
    return "Unknown";
}

} // namespace lmx::engine
