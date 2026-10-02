//----------------------------------------------------------------------------------------------------------------------
/// @file AppIcon.mm
/// @brief Names the native macOS application and loads the editor icon into it.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Shell/AppIcon.h"

#include "Core/Diagnostics/Log.h"

#import <AppKit/AppKit.h>

namespace lmx::app {

//======================================================================================================================
void applyApplicationName() {
    @autoreleasepool {
        CFMutableDictionaryRef info =
            (CFMutableDictionaryRef)CFBundleGetInfoDictionary(CFBundleGetMainBundle());
        if (info != nullptr)
            CFDictionarySetValue(info, kCFBundleNameKey, CFSTR("Luminex"));
    }
}

//======================================================================================================================
void applyApplicationIcon(const std::filesystem::path& png) {
    @autoreleasepool {
        NSString* path = [NSString stringWithUTF8String:png.c_str()];
        NSImage* image = [[NSImage alloc] initWithContentsOfFile:path];
        if (image == nil) {
            LMX_LOG_WARN("Application icon unavailable at '{}'; keeping the system icon. "
                         "Rebuild App to restore it.",
                         png.string());
            return;
        }
        [NSApp setApplicationIconImage:image];
    }
}

} // namespace lmx::app
