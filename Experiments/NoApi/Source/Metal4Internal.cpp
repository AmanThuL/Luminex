//----------------------------------------------------------------------------------------------------------------------
/// @file Metal4Internal.cpp
/// @brief Implements the conversion, address-resolution, and residency helpers the backend shares.
//----------------------------------------------------------------------------------------------------------------------

#include "Metal4Internal.h"

#include <algorithm>
#include <format>

namespace lmx::noapi {

//======================================================================================================================
NS::SharedPtr<NS::String> makeString(std::string_view text) {
    // string_view is not guaranteed NUL-terminated, so the text is owned before NS::String sees it.
    const std::string owned(text);
    return NS::TransferPtr(NS::String::alloc()->init(owned.c_str(), NS::UTF8StringEncoding));
}

//======================================================================================================================
std::string describe(const NS::Error* error) {
    if (error == nullptr) {
        return "no additional detail";
    }
    const NS::String* description = error->localizedDescription();
    const char* utf8 = description != nullptr ? description->utf8String() : nullptr;
    return utf8 != nullptr ? std::string(utf8) : std::string("no additional detail");
}

//======================================================================================================================
std::unexpected<Error> fail(ErrorCode code, std::string message) {
    return std::unexpected(Error{.code = code, .message = std::move(message)});
}

//======================================================================================================================
MTL::PixelFormat toMTL(Format format) {
    switch (format) {
    case Format::RGBA8Unorm:
        return MTL::PixelFormatRGBA8Unorm;
    case Format::RGBA8UnormSrgb:
        return MTL::PixelFormatRGBA8Unorm_sRGB;
    case Format::BGRA8Unorm:
        return MTL::PixelFormatBGRA8Unorm;
    case Format::RG16Float:
        return MTL::PixelFormatRG16Float;
    case Format::RGBA16Float:
        return MTL::PixelFormatRGBA16Float;
    case Format::R32Uint:
        return MTL::PixelFormatR32Uint;
    case Format::R32Float:
        return MTL::PixelFormatR32Float;
    case Format::RG11B10Float:
        return MTL::PixelFormatRG11B10Float;
    case Format::RGB10A2Unorm:
        return MTL::PixelFormatRGB10A2Unorm;
    case Format::D32Float:
        return MTL::PixelFormatDepth32Float;
    // Metal names the DXT1 formats BC1_RGBA: the one-bit-alpha and opaque encodings share one
    // pixel format and a block selects between them by the ordering of its endpoint colors.
    case Format::BC1RgbaUnorm:
        return MTL::PixelFormatBC1_RGBA;
    case Format::BC1RgbaUnormSrgb:
        return MTL::PixelFormatBC1_RGBA_sRGB;
    case Format::Undefined:
        break;
    }
    return MTL::PixelFormatInvalid;
}

//======================================================================================================================
MTL::CompareFunction toMTL(CompareOp op) {
    switch (op) {
    case CompareOp::Never:
        return MTL::CompareFunctionNever;
    case CompareOp::Less:
        return MTL::CompareFunctionLess;
    case CompareOp::Equal:
        return MTL::CompareFunctionEqual;
    case CompareOp::LessEqual:
        return MTL::CompareFunctionLessEqual;
    case CompareOp::Greater:
        return MTL::CompareFunctionGreater;
    case CompareOp::NotEqual:
        return MTL::CompareFunctionNotEqual;
    case CompareOp::GreaterEqual:
        return MTL::CompareFunctionGreaterEqual;
    case CompareOp::Always:
        break;
    }
    return MTL::CompareFunctionAlways;
}

//======================================================================================================================
MTL::Stages toStages(Stage stages) {
    if (stages == Stage::All) {
        return MTL::StageAll;
    }
    MTL::Stages result{};
    if (hasStage(stages, Stage::Copy)) {
        result |= kComputeStages;
    }
    if (hasStage(stages, Stage::Compute)) {
        result |= MTL::StageDispatch;
    }
    if (hasStage(stages, Stage::VertexShader)) {
        result |= MTL::StageVertex;
    }
    // Metal has no separate stage for attachment writes: they retire in the fragment stage, which
    // is also the only stage a depth write can be named through.
    if (hasStage(stages, Stage::PixelShader) || hasStage(stages, Stage::RasterColorOut) ||
        hasStage(stages, Stage::RasterDepthOut)) {
        result |= MTL::StageFragment;
    }
    return result;
}

//======================================================================================================================
MTL::Stages hazardStages(Hazard hazards) {
    MTL::Stages result{};
    // Indirect arguments are fetched before either stage of the consuming encoder runs, so the
    // dependency has to reach the earliest stage rather than the shader that reads the results.
    if (hasHazard(hazards, Hazard::DrawArguments)) {
        result |= MTL::StageVertex | MTL::StageDispatch;
    }
    if (hasHazard(hazards, Hazard::DepthStencil)) {
        result |= MTL::StageFragment;
    }
    // Hazard::Descriptors has no Metal counterpart: MTL4::VisibilityOptions offers no descriptor
    // cache flag, and descriptor visibility is implicit in the device-scope visibility every
    // barrier already requests.
    return result;
}

//======================================================================================================================
const AllocationRecord* findAllocation(const Device* device, GpuAddress address) {
    LMX_ASSERT(device != nullptr, "findAllocation: device must not be null");
    // Records are kept sorted by base, so the candidate is the last one starting at or below the
    // address.
    const auto upper = std::upper_bound(
        device->allocations.begin(), device->allocations.end(), address,
        [](GpuAddress value, const AllocationRecord& record) { return value < record.base; });
    if (upper == device->allocations.begin()) {
        return nullptr;
    }
    const AllocationRecord& record = *std::prev(upper);
    if (address >= record.base && address < record.base + record.size) {
        return &record;
    }
    return nullptr;
}

//======================================================================================================================
AllocationRecord* findAllocation(Device* device, GpuAddress address) {
    return const_cast<AllocationRecord*>(
        findAllocation(static_cast<const Device*>(device), address));
}

//======================================================================================================================
AddressResolution resolveAddress(const Device* device, GpuAddress address, std::string_view what) {
    const AllocationRecord* record = findAllocation(device, address);
    LMX_ASSERT(
        record != nullptr,
        std::format("{}: GPU address {:#x} lies outside every live allocation", what, address));
    LMX_ASSERT(record->buffer,
               std::format("{}: the allocation holding GPU address {:#x} has no buffer to address "
                           "it through",
                           what, address));
    const uint64_t offset = address - record->base;
    return {.buffer = record->buffer.get(),
            .offset = offset + record->bufferOffset,
            .remaining = record->size - offset};
}

//======================================================================================================================
void addResidency(Device* device, const MTL::Allocation* allocation, uint64_t bytes) {
    device->residency->addAllocation(allocation);
    device->residentBytes += bytes;
    device->residencyDirty = true;
}

//======================================================================================================================
void removeResidency(Device* device, const MTL::Allocation* allocation, uint64_t bytes) {
    device->residency->removeAllocation(allocation);
    LMX_ASSERT(device->residentBytes >= bytes,
               "removeResidency: the residency set is holding fewer bytes than this allocation");
    device->residentBytes -= bytes;
    device->residencyDirty = true;
}

//======================================================================================================================
void commitPendingResidency(Device* device) {
    if (!device->residencyDirty) {
        return;
    }
    // A commit republishes the whole set, so batching every pending change into one call is what
    // keeps residency off the per-allocation path.
    device->residency->commit();
    device->residencyDirty = false;
}

//======================================================================================================================
void drainDevice(Device* device) {
    if (device->submissions == 0) {
        return;
    }
    const bool signaled =
        device->timeline->waitUntilSignaledValue(device->submissions, kGpuTimeoutMs);
    LMX_ASSERT(signaled, "the GPU did not complete the prototype's submitted work within the "
                         "timeout");
}

//======================================================================================================================
void assertNoWorkInFlight(const Device* device, std::string_view what) {
    LMX_ASSERT(device->timeline->signaledValue() >= device->submissions,
               std::format("{}: {} submission(s) are still in flight -- every submission "
                           "referencing a destroyed object must be retired first",
                           what, device->submissions - device->timeline->signaledValue()));
}

} // namespace lmx::noapi
