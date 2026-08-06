#pragma once
// Shared plumbing for the Metal 4 backend: the metal-cpp umbrella includes plus the
// two conversions every backend file needs. Private to the RHI target -- metal-cpp
// types never appear in RHI.h.
#include "RHI/RHI.h"

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include <string>
#include <string_view>

namespace lmx::rhi::metal4 {

// RHI descs carry std::string_view, which is NOT guaranteed NUL-terminated, so the
// text is copied into a std::string before it reaches NS::String's char* initializer.
// Returns an owning (+1) reference; the SharedPtr releases it at end of scope, by
// which point whatever consumed it (setLabel, ...) has taken its own reference.
inline NS::SharedPtr<NS::String> makeString(std::string_view text) {
    const std::string owned(text);
    return NS::TransferPtr(NS::String::alloc()->init(owned.c_str(), NS::UTF8StringEncoding));
}

inline MTL::PixelFormat toMTL(Format format) {
    switch (format) {
    case Format::BGRA8Unorm:
        return MTL::PixelFormatBGRA8Unorm;
    case Format::RGBA8Unorm:
        return MTL::PixelFormatRGBA8Unorm;
    case Format::D32Float:
        return MTL::PixelFormatDepth32Float;
    case Format::Unknown:
        break;
    }
    return MTL::PixelFormatInvalid;
}

} // namespace lmx::rhi::metal4
