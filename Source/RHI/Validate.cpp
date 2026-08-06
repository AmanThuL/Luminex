#include "RHI/Validate.h"

namespace lmx::rhi {
namespace {

Result<void> invalid(const char* message) {
    return std::unexpected(Error{ErrorCode::InvalidDesc, message});
}

// Texture::readback() copies 4 bytes per pixel, which only holds for these.
bool isEightBitFormat(Format format) {
    return format == Format::BGRA8Unorm || format == Format::RGBA8Unorm;
}

} // namespace

Result<void> validate(const BufferDesc& desc) {
    if (desc.size == 0) {
        return invalid("BufferDesc.size must be greater than zero");
    }
    return {};
}

Result<void> validate(const TextureDesc& desc) {
    if (desc.width == 0) {
        return invalid("TextureDesc.width must be greater than zero");
    }
    if (desc.height == 0) {
        return invalid("TextureDesc.height must be greater than zero");
    }
    if (desc.format == Format::Unknown) {
        return invalid("TextureDesc.format must not be Format::Unknown");
    }
    if (desc.cpuReadback && !isEightBitFormat(desc.format)) {
        return invalid("TextureDesc.cpuReadback: readback supports 8-bit formats only");
    }
    return {};
}

Result<void> validate(const GraphicsPipelineDesc& desc) {
    if (desc.library == nullptr) {
        return invalid("GraphicsPipelineDesc.library must not be null");
    }
    if (desc.vertexEntry.empty()) {
        return invalid("GraphicsPipelineDesc.vertexEntry must not be empty");
    }
    if (desc.fragmentEntry.empty()) {
        return invalid("GraphicsPipelineDesc.fragmentEntry must not be empty");
    }
    if (desc.colorFormat == Format::Unknown) {
        return invalid("GraphicsPipelineDesc.colorFormat must not be Format::Unknown");
    }
    return {};
}

Result<void> validate(const SwapchainDesc& desc) {
    if (desc.nativeLayer == nullptr) {
        return invalid("SwapchainDesc.nativeLayer must not be null");
    }
    if (desc.width == 0) {
        return invalid("SwapchainDesc.width must be greater than zero");
    }
    if (desc.height == 0) {
        return invalid("SwapchainDesc.height must be greater than zero");
    }
    if (desc.format == Format::Unknown) {
        return invalid("SwapchainDesc.format must not be Format::Unknown");
    }
    return {};
}

} // namespace lmx::rhi
