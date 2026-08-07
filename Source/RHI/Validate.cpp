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

// Metal 4 / Apple7+ GPU family cap on a 2D texture's width and height (Metal Feature Set
// Tables, "Maximum 2D texture width and height"; M1 requires MTLGPUFamilyMetal4, which implies
// this family on every supported device). metal-cpp's MTL::Device exposes no
// maxTextureDimension2D()-style query to read this back at runtime (checked ThirdParty/metal-cpp/
// Metal/MTLDevice.hpp during Task 14: only maxBufferLength() exists), so the limit is hardcoded
// here rather than derived from the device. Exceeding it is not a diagnosable Metal error -- the
// MTLTextureDescriptor validator aborts the process outright -- so it has to be caught here,
// before a TextureDesc ever reaches the backend.
constexpr uint32_t kMaxTextureDimension2D = 16384;

// Formats that may back a color attachment or a swapchain surface. A depth format in either
// place is fatal rather than recoverable further down: Metal's render-pipeline descriptor
// validator aborts the process on "MTLPixelFormatDepth32Float is not color renderable", and
// CAMetalLayer rejects a depth pixel format outright. Both are reachable from the public RHI
// with a perfectly well-formed desc, so the whitelist has to live here.
bool isColorRenderableFormat(Format format) {
    return format == Format::BGRA8Unorm || format == Format::RGBA8Unorm;
}

// Formats that may back a depth attachment. D32Float is the only depth format the RHI
// exposes today; the predicate exists so that adding D24S8 or D16 is a one-line change here
// rather than a hunt through every depth check.
bool isDepthFormat(Format format) {
    return format == Format::D32Float;
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
    if (desc.width > kMaxTextureDimension2D) {
        return invalid("TextureDesc.width exceeds the maximum 2D texture dimension (16384)");
    }
    if (desc.height == 0) {
        return invalid("TextureDesc.height must be greater than zero");
    }
    if (desc.height > kMaxTextureDimension2D) {
        return invalid("TextureDesc.height exceeds the maximum 2D texture dimension (16384)");
    }
    if (desc.format == Format::Unknown) {
        return invalid("TextureDesc.format must not be Format::Unknown");
    }
    if (desc.renderTarget && !isColorRenderableFormat(desc.format) && !isDepthFormat(desc.format)) {
        return invalid("TextureDesc.renderTarget requires a color-renderable or depth format");
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
    if (!isColorRenderableFormat(desc.colorFormat)) {
        return invalid("GraphicsPipelineDesc.colorFormat must be a color-renderable format "
                       "(BGRA8Unorm or RGBA8Unorm)");
    }
    if (desc.depthFormat != Format::Unknown && !isDepthFormat(desc.depthFormat)) {
        return invalid("GraphicsPipelineDesc.depthFormat must be a depth format (D32Float) or "
                       "Format::Unknown for a depth-less pipeline");
    }
    if ((desc.depthTestEnable || desc.depthWriteEnable) && desc.depthFormat == Format::Unknown) {
        return invalid("GraphicsPipelineDesc.depthFormat must be set when depth test/write is "
                       "enabled");
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
    if (!isColorRenderableFormat(desc.format)) {
        return invalid(
            "SwapchainDesc.format must be a color-renderable format (BGRA8Unorm or RGBA8Unorm)");
    }
    return {};
}

} // namespace lmx::rhi
