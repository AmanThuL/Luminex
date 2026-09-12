//----------------------------------------------------------------------------------------------------------------------
/// @file EdrProbe.mm
/// @brief Configures and observes the isolated macOS extended-range probe.
//----------------------------------------------------------------------------------------------------------------------
#include "App/EdrProbe.h"
#include <SDL3/SDL.h>
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

namespace lmx::experimental::edr {
//======================================================================================================================
bool configure(void* raw, bool extended) {
    CAMetalLayer* layer = (__bridge CAMetalLayer*)raw;
    CGColorSpaceRef space = CGColorSpaceCreateWithName(extended ? kCGColorSpaceExtendedLinearSRGB : kCGColorSpaceSRGB);
    if (!space) return false;
    layer.colorspace = space;
    CGColorSpaceRelease(space);
    layer.wantsExtendedDynamicRangeContent = extended;
    return layer.wantsExtendedDynamicRangeContent == extended;
}

//======================================================================================================================
Headroom observe(SDL_Window* window) {
    const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
    NSWindow* native = (__bridge NSWindow*)SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    NSScreen* screen = native.screen;
    Headroom result;
    result.enabled = SDL_GetBooleanProperty(properties, SDL_PROP_WINDOW_HDR_ENABLED_BOOLEAN, false);
    result.white = SDL_GetFloatProperty(properties, SDL_PROP_WINDOW_SDR_WHITE_LEVEL_FLOAT, 1);
    result.current = SDL_GetFloatProperty(properties, SDL_PROP_WINDOW_HDR_HEADROOM_FLOAT, 1);
    result.nativeCurrent = screen.maximumExtendedDynamicRangeColorComponentValue;
    result.potential = screen.maximumPotentialExtendedDynamicRangeColorComponentValue;
    result.reference = screen.maximumReferenceExtendedDynamicRangeColorComponentValue;
    return result;
}
}
