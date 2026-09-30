//----------------------------------------------------------------------------------------------------------------------
/// @file AppAppearance.mm
/// @brief Bridges SDL appearance and macOS accessibility and viewport window themes.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Shell/AppAppearance.h"

#import <AppKit/AppKit.h>
#include <SDL3/SDL.h>
#include <imgui.h>

namespace lmx::app {

//======================================================================================================================
SystemTheme systemTheme() {
    switch (SDL_GetSystemTheme()) {
    case SDL_SYSTEM_THEME_LIGHT:
        return SystemTheme::Light;
    case SDL_SYSTEM_THEME_DARK:
        return SystemTheme::Dark;
    default:
        return SystemTheme::Unknown;
    }
}

//======================================================================================================================
bool reduceMotion() {
    return NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion;
}

//======================================================================================================================
void applyViewportAppearance(std::optional<ThemeKind> appearance) {
    @autoreleasepool {
        NSAppearance* nativeAppearance = nil;
        if (appearance) {
            nativeAppearance = [NSAppearance appearanceNamed:*appearance == ThemeKind::Light
                                                                 ? NSAppearanceNameAqua
                                                                 : NSAppearanceNameDarkAqua];
        }
        for (ImGuiViewport* viewport : ImGui::GetPlatformIO().Viewports) {
            NSWindow* window = (__bridge NSWindow*)viewport->PlatformHandleRaw;
            if (window == nil) {
                continue;
            }
            NSAppearance* current = window.appearance;
            if (current != nativeAppearance &&
                ![current.name isEqualToString:nativeAppearance.name]) {
                window.appearance = nativeAppearance;
            }
        }
    }
}

} // namespace lmx::app
