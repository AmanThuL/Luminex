//----------------------------------------------------------------------------------------------------------------------
/// @file AppIcon.mm
/// @brief Loads the editor icon into the native macOS application.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Shell/AppIcon.h"

#include "Core/Diagnostics/Log.h"

#import <AppKit/AppKit.h>

namespace lmx::app {

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
