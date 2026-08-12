//----------------------------------------------------------------------------------------------------------------------
/// @file DepthStencilState.cpp
/// @brief Implements depth and stencil state as an object separate from the pipeline.
//----------------------------------------------------------------------------------------------------------------------

#include "Metal4Internal.h"

#include <string>

namespace lmx::experimental::noapi {
namespace {

//======================================================================================================================
MTL::StencilOperation toMTL(StencilOp op) {
    switch (op) {
    case StencilOp::Zero:
        return MTL::StencilOperationZero;
    case StencilOp::Replace:
        return MTL::StencilOperationReplace;
    case StencilOp::Increment:
        return MTL::StencilOperationIncrementClamp;
    case StencilOp::Decrement:
        return MTL::StencilOperationDecrementClamp;
    case StencilOp::Invert:
        return MTL::StencilOperationInvert;
    case StencilOp::Keep:
        break;
    }
    return MTL::StencilOperationKeep;
}

//======================================================================================================================
NS::SharedPtr<MTL::StencilDescriptor> makeStencil(const StencilDesc& desc,
                                                  const DepthStencilDesc& owner) {
    auto stencil = NS::TransferPtr(MTL::StencilDescriptor::alloc()->init());
    stencil->setStencilCompareFunction(toMTL(desc.test));
    stencil->setStencilFailureOperation(toMTL(desc.failOp));
    stencil->setDepthStencilPassOperation(toMTL(desc.passOp));
    stencil->setDepthFailureOperation(toMTL(desc.depthFailOp));
    stencil->setReadMask(owner.stencilReadMask);
    stencil->setWriteMask(owner.stencilWriteMask);
    return stencil;
}

} // namespace

//======================================================================================================================
Result<DepthStencilState*> createDepthStencilState(Device* device, const DepthStencilDesc& desc) {
    LMX_ASSERT(device != nullptr, "createDepthStencilState: device must not be null");
    LMX_ASSERT(!desc.label.empty(), "createDepthStencilState: label must not be empty");
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    auto stateDesc = NS::TransferPtr(MTL::DepthStencilDescriptor::alloc()->init());
    // Metal has no depth-test enable: comparing always is what a disabled test means.
    stateDesc->setDepthCompareFunction(desc.depthTestEnabled ? toMTL(desc.depthTest)
                                                             : MTL::CompareFunctionAlways);
    stateDesc->setDepthWriteEnabled(desc.depthWriteEnabled);
    if (desc.stencilEnabled) {
        stateDesc->setFrontFaceStencil(makeStencil(desc.front, desc).get());
        stateDesc->setBackFaceStencil(makeStencil(desc.back, desc).get());
    }
    stateDesc->setLabel(makeString(desc.label).get());

    auto state = std::make_unique<DepthStencilState>();
    state->handle = NS::TransferPtr(device->mtl->newDepthStencilState(stateDesc.get()));
    if (!state->handle) {
        return fail(ErrorCode::ResourceCreationFailed,
                    "createDepthStencilState: the device rejected state '" +
                        std::string(desc.label) + "'");
    }

    device->liveDepthStates += 1;
    return state.release();
}

//======================================================================================================================
void destroyDepthStencilState(Device* device, DepthStencilState* state) {
    LMX_ASSERT(device != nullptr, "destroyDepthStencilState: device must not be null");
    LMX_ASSERT(state != nullptr, "destroyDepthStencilState: state must not be null");
    assertNoWorkInFlight(device, "destroyDepthStencilState");

    LMX_ASSERT(device->liveDepthStates > 0,
               "destroyDepthStencilState: no depth-stencil state is live on this device");
    device->liveDepthStates -= 1;
    delete state;
}

} // namespace lmx::experimental::noapi
