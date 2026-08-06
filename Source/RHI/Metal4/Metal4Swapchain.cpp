#include "RHI/Metal4/Metal4Swapchain.h"

#include "Core/Assert.h"
#include "Core/Log.h"

#include <string>
#include <utility>

namespace lmx::rhi::metal4 {

Metal4Swapchain::Metal4Swapchain(NS::SharedPtr<CA::MetalLayer> layer,
                                 NS::SharedPtr<MTL4::CommandQueue> queue,
                                 MTL::ResidencySet* layerResidency)
    : m_layer(std::move(layer)), m_queue(std::move(queue)), m_layerResidency(layerResidency) {
    // The layer keeps its drawables' allocations in a residency set of its own, and Metal 4
    // makes nothing resident implicitly -- so the queue needs this set in addition to the
    // device's. It is attached for the swapchain's whole lifetime rather than per frame: the
    // set's *contents* are the layer's business, only the queue edge is ours.
    if (m_layerResidency != nullptr) {
        m_queue->addResidencySet(m_layerResidency);
    }
}

Metal4Swapchain::~Metal4Swapchain() {
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    if (m_layerResidency != nullptr) {
        m_queue->removeResidencySet(m_layerResidency);
    }
    // A drawable still held here means the caller acquired without ever ending the frame.
    // Releasing it un-presented is the correct teardown -- the layer reclaims it.
    m_texture.reset();
    m_drawable.reset();
}

Result<Texture*> Metal4Swapchain::acquireNextTexture() {
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    LMX_ASSERT(!m_drawable, "acquireNextTexture: the previous drawable has not been presented "
                            "yet -- call Device::endFrame(swapchain) first");

    // Unlike every other failure in this backend, this one is genuinely recoverable and
    // expected: CAMetalLayer returns nil when all its drawables are still in flight (or the
    // 1-second allowsNextDrawableTimeout elapses), and the right response is to skip the frame.
    CA::MetalDrawable* drawable = m_layer->nextDrawable();
    if (drawable == nullptr) {
        return std::unexpected(
            Error{ErrorCode::SwapchainFailed, "no drawable available from CAMetalLayer"});
    }
    MTL::Texture* texture = drawable->texture();
    if (texture == nullptr) {
        return std::unexpected(
            Error{ErrorCode::SwapchainFailed, "CAMetalLayer drawable has no texture"});
    }

    m_drawable = NS::RetainPtr(drawable);
    // Dimensions come from the texture, not from the desc: after a resize the layer may still
    // hand out one drawable at the previous size, and the render pass has to match the texture
    // it actually got.
    //
    // The null residency set is the documented opt-out (Metal4Resources.h): these textures are
    // owned by CAMetalLayer and recycled every frame, and registering them would churn the
    // device's residency set at frame rate. Their residency comes from the layer's own set,
    // attached to the queue in the constructor.
    m_texture = std::make_unique<Metal4Texture>(
        NS::RetainPtr(texture), static_cast<uint32_t>(texture->width()),
        static_cast<uint32_t>(texture->height()), /*cpuReadback=*/false,
        /*residency=*/NS::SharedPtr<MTL::ResidencySet>{});
    return m_texture.get();
}

void Metal4Swapchain::resize(uint32_t width, uint32_t height) {
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    LMX_ASSERT(width > 0 && height > 0, "Swapchain::resize: width and height must be positive");

    // No waitIdle: this destroys nothing. The drawables are the layer's, it retires the
    // old-size ones on its own schedule, and in-flight command buffers keep referencing the
    // textures they were encoded against. A resize that tore down RHI-owned attachments would
    // have to drain the GPU first -- that arrives with M2's render graph, not here.
    m_layer->setDrawableSize(CGSize{static_cast<CGFloat>(width), static_cast<CGFloat>(height)});
    LMX_LOG_INFO("swapchain resized to {}x{}", width, height);
}

void Metal4Swapchain::releaseCurrentDrawable() {
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
    m_texture.reset();
    m_drawable.reset();
}

} // namespace lmx::rhi::metal4
