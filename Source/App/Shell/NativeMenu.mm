//----------------------------------------------------------------------------------------------------------------------
/// @file NativeMenu.mm
/// @brief Renders model menus in AppKit and preserves SDL keyboard ownership.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Shell/NativeMenu.h"

#include "Core/Diagnostics/Log.h"

#import <AppKit/AppKit.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <format>
#include <utility>

using namespace lmx::app;

namespace {

// Marks rows this file builds in NSApp.windowsMenu, where AppKit appends rows of its own.
constexpr NSInteger kOwnedWindowRow = 0x4C4D5857;

struct QueuedCommand {
    std::pair<MenuCommand, uint32_t> command;
    ImGuiKey key = ImGuiKey_None;
    ImGuiKeyChord modifiers = 0;
    ImGuiID viewport = 0;
    unsigned int firstEvent = 0;
    unsigned int inputEvent = 0;
    bool otherSurfaceFocused = false;
    bool cameraLook = false;
    bool resolved = true;
    bool allowed = true;
    std::string reason;
};

//======================================================================================================================
ImGuiKey shortcutKey(MenuCommand command, NSEvent* event) {
    switch (command) {
    case MenuCommand::FrameSelected:
        return ImGuiKey_F;
    case MenuCommand::ResetCamera:
        return ImGuiKey_Home;
    case MenuCommand::Capture:
        return ImGuiKey_C;
    case MenuCommand::Open:
        return ImGuiKey_O;
    case MenuCommand::Save:
    case MenuCommand::SaveAs:
        return ImGuiKey_S;
    case MenuCommand::Quit:
        return ImGuiKey_Q;
    case MenuCommand::ZoomIn:
        return event.keyCode == 69 ? ImGuiKey_KeypadAdd : ImGuiKey_Equal;
    case MenuCommand::ZoomOut:
        return event.keyCode == 78 ? ImGuiKey_KeypadSubtract : ImGuiKey_Minus;
    case MenuCommand::ResetUiScale:
        return event.keyCode == 82 ? ImGuiKey_Keypad0 : ImGuiKey_0;
    default:
        return ImGuiKey_None;
    }
}

//======================================================================================================================
NSString* nativeString(const std::string& value) {
    return [NSString stringWithUTF8String:value.c_str()];
}

//======================================================================================================================
ImGuiViewport* keyViewport() {
    NSWindow* window = NSApp.keyWindow;
    if (!window || window.attachedSheet || !ImGui::GetCurrentContext())
        return nullptr;
    // SDL's Cocoa_StartTextInput installs its translator under the content view. Other Cocoa
    // responders (including NSTextView and chooser field editors) keep their native editing.
    NSResponder* responder = window.firstResponder;
    const bool sdlText = [responder isKindOfClass:NSClassFromString(@"SDL3TranslatorResponder")] &&
                         [(NSView*)responder isDescendantOf:window.contentView];
    if (responder != window && responder != window.contentView && !sdlText)
        return nullptr;
    for (auto* viewport : ImGui::GetPlatformIO().Viewports)
        if (viewport->PlatformHandleRaw == (__bridge void*)window)
            return viewport;
    return nullptr;
}

//======================================================================================================================
bool focusedTextField(const ShortcutContext& context) {
    auto* viewport = keyViewport();
    if (!viewport || !context.textFieldFocused)
        return false;
    auto& gui = *ImGui::GetCurrentContext();
    return ImGui::GetInputTextState(gui.ActiveId) && gui.ActiveIdWindow &&
           gui.ActiveIdWindow->Viewport == viewport;
}

//======================================================================================================================
bool matches(const Shortcut& shortcut, NSEvent* event) {
    const auto modifiers = event.modifierFlags;
    if (modifiers & (NSEventModifierFlagControl | NSEventModifierFlagOption))
        return false;
    if (bool(modifiers & NSEventModifierFlagCommand) != shortcut.command)
        return false;
    NSString* key = event.charactersIgnoringModifiers.lowercaseString;
    // Preserve the physical Equal/Minus/0 and keypad aliases of the SDL polling route.
    if (shortcut.key == "+")
        return
            [key isEqual:@"+"] || [key isEqual:@"="] || event.keyCode == 24 || event.keyCode == 69;
    if (shortcut.key == "-")
        return [key isEqual:@"-"] || event.keyCode == 27 || event.keyCode == 78;
    if (shortcut.key == "0")
        return [key isEqual:@"0"] || event.keyCode == 29 || event.keyCode == 82;
    // Shift selects Save As; the other existing bindings accept Shift too.
    if (shortcut.key == "S" && bool(modifiers & NSEventModifierFlagShift) != shortcut.shift)
        return false;
    return [key
        isEqual:shortcut.key == "Home" ? @"\uF729" : nativeString(shortcut.key).lowercaseString];
}

//======================================================================================================================
const MenuItem* findCommand(const std::vector<MenuItem>& items, MenuCommand command,
                            uint32_t argument) {
    for (const auto& item : items) {
        if (item.command == command && item.argument == argument)
            return &item;
        if (const auto* found = findCommand(item.children, command, argument))
            return found;
    }
    return nullptr;
}

//======================================================================================================================
std::string commandName(const std::vector<MenuItem>& items,
                        const std::pair<MenuCommand, uint32_t>& command) {
    const auto* item = findCommand(items, command.first, command.second);
    return item ? item->label : std::format("command {}", static_cast<int>(command.first));
}

} // namespace

@interface LMXNativeSubmenu : NSMenu {
@public
    std::vector<size_t> path;
    int kind; // Model, application, Edit.
}
@end
@interface LMXNativeMenuDelegate : NSObject <NSMenuDelegate> {
@public
    std::vector<MenuItem> model;
    ShortcutContext context;
    std::vector<QueuedCommand> commands;
    std::optional<QueuedCommand> shortcutCommand;
    NSEvent* shortcutEvent;
    NSEvent* lastShortcutEvent;
}
- (void)choose:(NSMenuItem*)sender;
- (void)chooseShortcut:(id)sender;
- (void)chooseQuit:(id)sender;
- (void)about:(id)sender;
- (void)observeShortcut;
- (void)edit:(NSMenuItem*)sender;
- (NSMenuItem*)row:(const MenuItem&)item path:(const std::vector<size_t>&)path;
@end

@implementation LMXNativeSubmenu
- (BOOL)performKeyEquivalent:(NSEvent*)event {
    // NSMenu can fall back to item key equivalents after a delegate refusal. Keep displayed
    // shortcuts while making the delegate's focus decision authoritative for the entire tree.
    id target = nil;
    SEL action = nil;
    if (![self.delegate menuHasKeyEquivalent:self forEvent:event target:&target action:&action]) {
        // Refusal describes the published policy. Observe the raw intent separately so a pending
        // click or model change can resolve it against the frame that actually consumes the key.
        [(LMXNativeMenuDelegate*)self.delegate observeShortcut];
        return NO;
    }
    const BOOL sent = [NSApp sendAction:action to:target from:self];
    // A shortcut action stages an intent. SDL must still deliver the original key so ImGui
    // resolves its owner after pending clicks, navigation and popups have been processed.
    return action == @selector(chooseShortcut:) ? NO : sent;
}
@end

@implementation LMXNativeMenuDelegate

- (NSMenuItem*)row:(const MenuItem&)item path:(const std::vector<size_t>&)path {
    if (item.separator)
        return NSMenuItem.separatorItem;
    NSMenuItem* row = [[NSMenuItem alloc] initWithTitle:nativeString(item.label)
                                                 action:item.command ? @selector(choose:) : nil
                                          keyEquivalent:@""];
    row.target = self;
    row.enabled = item.enabled && (item.command || !item.children.empty());
    row.state = item.checked ? NSControlStateValueOn : NSControlStateValueOff;
    row.toolTip = item.enabled ? nil : nativeString(item.disabledReason);
    if (item.command)
        row.representedObject = @[ @(static_cast<int>(*item.command)), @(item.argument) ];
    if (item.shortcut) {
        const auto& shortcut = *item.shortcut;
        row.keyEquivalent =
            shortcut.key == "Home" ? @"\uF729" : nativeString(shortcut.key).lowercaseString;
        row.keyEquivalentModifierMask = (shortcut.command ? NSEventModifierFlagCommand : 0) |
                                        (shortcut.shift ? NSEventModifierFlagShift : 0);
    }
    if (!item.children.empty()) {
        LMXNativeSubmenu* submenu = [[LMXNativeSubmenu alloc] initWithTitle:row.title];
        submenu->path = path;
        submenu.delegate = self;
        submenu.autoenablesItems = NO;
        row.submenu = submenu;
    }
    return row;
}

- (void)menuNeedsUpdate:(NSMenu*)menu {
    if (![menu isKindOfClass:LMXNativeSubmenu.class])
        return;
    auto* submenu = (LMXNativeSubmenu*)menu;
    if (submenu->kind == -1)
        return;
    // AppKit lists open windows, and on recent systems tiling commands, in the windows menu.
    // Those rows are not rebuilt here, so only the rows this delegate added are replaced.
    const bool windows = menu == NSApp.windowsMenu;
    if (windows) {
        for (NSInteger i = menu.numberOfItems - 1; i >= 0; --i)
            if ([menu itemAtIndex:i].tag == kOwnedWindowRow)
                [menu removeItemAtIndex:i];
    } else {
        [menu removeAllItems];
    }
    if (submenu->kind == 1) {
        [menu addItemWithTitle:@"About Luminex" action:@selector(about:) keyEquivalent:@""].target =
            self;
        [menu addItem:NSMenuItem.separatorItem];
        [menu addItemWithTitle:@"Hide Luminex" action:@selector(hide:) keyEquivalent:@"h"].target =
            NSApp;
        NSMenuItem* others = [menu addItemWithTitle:@"Hide Others"
                                             action:@selector(hideOtherApplications:)
                                      keyEquivalent:@"h"];
        others.target = NSApp;
        others.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
        [menu addItemWithTitle:@"Show All"
                        action:@selector(unhideAllApplications:)
                 keyEquivalent:@""]
            .target = NSApp;
        [menu addItem:NSMenuItem.separatorItem];
        if (const auto* quit = findCommand(model, MenuCommand::Quit, 0))
            [menu addItem:[self row:*quit path:{}]];
        return;
    }
    if (submenu->kind == 2) {
        NSArray* titles = @[ @"Cut", @"Copy", @"Paste", @"Select All" ];
        NSArray* keys = @[ @"x", @"c", @"v", @"a" ];
        for (NSUInteger i = 0; i < titles.count; ++i) {
            NSMenuItem* item = [menu addItemWithTitle:titles[i]
                                               action:@selector(edit:)
                                        keyEquivalent:keys[i]];
            item.target = self;
            item.tag = i;
            item.enabled = focusedTextField(context);
            item.toolTip = item.enabled ? nil : @"No text field has focus";
        }
        return;
    }
    const auto* items = &model;
    for (const auto index : submenu->path) {
        if (index >= items->size())
            return;
        items = &(*items)[index].children;
    }
    NSInteger owned = 0;
    const auto add = [&](NSMenuItem* row) {
        if (!windows) {
            [menu addItem:row];
            return;
        }
        row.tag = kOwnedWindowRow;
        [menu insertItem:row atIndex:owned++];
    };
    for (size_t i = 0; i < items->size(); ++i) {
        const auto& item = (*items)[i];
        if (item.command == MenuCommand::Quit)
            continue;
        auto path = submenu->path;
        path.push_back(i);
        add([self row:item path:path]);
    }
    if (!windows) {
        if (menu.itemArray.lastObject.separatorItem)
            [menu removeItemAtIndex:menu.numberOfItems - 1];
        return;
    }
    // Standard window commands use a nil target: the responder chain delivers them to the key
    // window, which is the main window or a detached panel. That window also validates them.
    NSWindow* key = NSApp.keyWindow;
    NSArray* titles = @[
        @"Minimize", @"Zoom",
        key.styleMask & NSWindowStyleMaskFullScreen ? @"Exit Full Screen" : @"Enter Full Screen",
        @"Close"
    ];
    NSArray* keys = @[ @"m", @"", @"f", @"w" ];
    const SEL actions[] = {@selector(performMiniaturize:), @selector(performZoom:),
                           @selector(toggleFullScreen:), @selector(performClose:)};
    add(NSMenuItem.separatorItem);
    for (NSUInteger i = 0; i < titles.count; ++i) {
        NSMenuItem* row = [[NSMenuItem alloc] initWithTitle:titles[i]
                                                     action:actions[i]
                                              keyEquivalent:keys[i]];
        if (actions[i] == @selector(toggleFullScreen:))
            row.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagControl;
        id responder = [NSApp targetForAction:actions[i] to:nil from:row];
        row.enabled =
            responder && (![responder conformsToProtocol:@protocol(NSMenuItemValidation)] ||
                          [(id<NSMenuItemValidation>)responder validateMenuItem:row]);
        row.toolTip = row.enabled ? nil : @"No window that supports this command has focus";
        add(row);
    }
    if (menu.numberOfItems > owned && ![menu itemAtIndex:owned].separatorItem)
        add(NSMenuItem.separatorItem);
}

- (void)choose:(NSMenuItem*)sender {
    NSArray* identity = sender.representedObject;
    const auto command = static_cast<MenuCommand>([identity[0] intValue]);
    const uint32_t argument = [identity[1] unsignedIntValue];
    const auto* item = findCommand(model, command, argument);
    if (item && item->enabled)
        commands.push_back({.command = {command, argument}});
}

- (void)chooseShortcut:(id)sender {
    (void)sender;
    [self observeShortcut];
}

- (void)chooseQuit:(id)sender {
    (void)sender;
    if (lastShortcutEvent == shortcutEvent)
        return;
    commands.push_back({.command = {MenuCommand::Quit, 0}});
    lastShortcutEvent = shortcutEvent;
}

- (void)about:(id)sender {
    (void)sender;
    // The unbundled binary has no Info.plist: name the application and pass the icon AppIcon
    // applied, and blank both version fields rather than show values nothing defines. The
    // credits restate the README's opening description, one sentence per panel line.
    NSMutableParagraphStyle* centered = [[NSMutableParagraphStyle alloc] init];
    centered.alignment = NSTextAlignmentCenter;
    NSAttributedString* description = [[NSAttributedString alloc]
        initWithString:@"Physically based rendering on Apple Silicon.\nBuilt directly on Metal 4."
            attributes:@{
                NSFontAttributeName : [NSFont systemFontOfSize:NSFont.smallSystemFontSize],
                NSForegroundColorAttributeName : NSColor.labelColor,
                NSParagraphStyleAttributeName : centered,
            }];
    [NSApp orderFrontStandardAboutPanelWithOptions:@{
        NSAboutPanelOptionApplicationName : @"Luminex",
        NSAboutPanelOptionApplicationIcon : NSApp.applicationIconImage,
        NSAboutPanelOptionApplicationVersion : @"",
        NSAboutPanelOptionVersion : @"",
        NSAboutPanelOptionCredits : description,
    }];
}

- (void)observeShortcut {
    if (shortcutCommand && lastShortcutEvent != shortcutEvent) {
        commands.push_back(*shortcutCommand);
        lastShortcutEvent = shortcutEvent;
    }
    shortcutCommand.reset();
}

- (BOOL)menuHasKeyEquivalent:(NSMenu*)menu
                    forEvent:(NSEvent*)event
                      target:(id*)target
                      action:(SEL*)action {
    (void)menu;
    *target = nil;
    *action = nil;
    shortcutCommand.reset();
    if (event.type != NSEventTypeKeyDown || event.isARepeat)
        return NO;
    const auto modifiers =
        event.modifierFlags & (NSEventModifierFlagCommand | NSEventModifierFlagControl |
                               NSEventModifierFlagOption | NSEventModifierFlagShift);
    if ([event.charactersIgnoringModifiers.lowercaseString isEqual:@"h"] &&
        (modifiers == NSEventModifierFlagCommand ||
         modifiers == (NSEventModifierFlagCommand | NSEventModifierFlagOption))) {
        *target = NSApp;
        *action = modifiers & NSEventModifierFlagOption ? @selector(hideOtherApplications:)
                                                        : @selector(hide:);
        return YES;
    }
    // Window commands are not text-editing chords: they stay live in fields, popups and
    // detached windows, and reach the key window through the responder chain.
    NSString* key = event.charactersIgnoringModifiers.lowercaseString;
    const SEL windowAction =
        modifiers == NSEventModifierFlagCommand && [key isEqual:@"w"] ? @selector(performClose:)
        : modifiers == NSEventModifierFlagCommand && [key isEqual:@"m"]
            ? @selector(performMiniaturize:)
        : modifiers == (NSEventModifierFlagCommand | NSEventModifierFlagControl) &&
                [key isEqual:@"f"]
            ? @selector(toggleFullScreen:)
            : nil;
    if (windowAction) {
        if (lastShortcutEvent == event)
            return NO;
        lastShortcutEvent = event;
        *action = windowAction;
        return YES;
    }
    const auto visit = [&](auto&& self, const std::vector<MenuItem>& items) -> const MenuItem* {
        for (const auto& item : items) {
            if (item.command && item.shortcut && matches(*item.shortcut, event))
                return &item;
            if (const auto* match = self(self, item.children))
                return match;
        }
        return nullptr;
    };
    if (const auto* item = visit(visit, model)) {
        const auto command = *item->command;
        if (shortcutPolicy(command) == EditorShortcut::Quit) {
            // Quit answers from any key window, sheet or responder, so it queues a ready command
            // instead of an intent that must bind to an ImGui key event in the main viewport.
            if (keyboardDecision(model, command, item->argument, context).outcome !=
                KeyboardOutcome::Run)
                return NO;
            shortcutEvent = event;
            *target = self;
            *action = @selector(chooseQuit:);
            return YES;
        }
        ImGuiViewport* viewport = keyViewport();
        if (!viewport || lastShortcutEvent == event)
            return NO;
        shortcutEvent = event;
        shortcutCommand = QueuedCommand{
            .command = {*item->command, item->argument},
            .key = shortcutKey(*item->command, event),
            .modifiers =
                ((event.modifierFlags & NSEventModifierFlagCommand)
                     ? (ImGui::GetIO().ConfigMacOSXBehaviors ? ImGuiMod_Ctrl : ImGuiMod_Super)
                     : 0) |
                ((event.modifierFlags & NSEventModifierFlagShift) ? ImGuiMod_Shift : 0),
            .viewport = viewport->ID,
            .firstEvent = ImGui::GetCurrentContext()->InputEventsNextEventId,
            .otherSurfaceFocused = (viewport->Flags & ImGuiViewportFlags_NoAutoMerge) != 0,
            .cameraLook = (NSEvent.pressedMouseButtons & 2) != 0,
            .resolved = false};
        // Availability and focus here describe the published menu, never execution eligibility.
        // The consuming frame rechecks both, including disabled ancestors and capture recovery.
        auto focus = context;
        focus.otherSurfaceFocused |= shortcutCommand->otherSurfaceFocused;
        focus.cameraLook |= shortcutCommand->cameraLook;
        if (command == MenuCommand::ZoomIn || command == MenuCommand::ZoomOut ||
            command == MenuCommand::ResetUiScale)
            focus.otherSurfaceFocused = false;
        const auto outcome = keyboardDecision(model, command, item->argument, focus).outcome;
        if (outcome != KeyboardOutcome::Run && outcome != KeyboardOutcome::Report)
            return NO;
        *target = self;
        *action = @selector(chooseShortcut:);
        return YES;
    }
    return NO;
}

- (void)edit:(NSMenuItem*)sender {
    if (!focusedTextField(context))
        return;
    constexpr SDL_Scancode codes[] = {SDL_SCANCODE_X, SDL_SCANCODE_C, SDL_SCANCODE_V,
                                      SDL_SCANCODE_A};
    constexpr SDL_Keycode keys[] = {SDLK_X, SDLK_C, SDLK_V, SDLK_A};
    auto* viewport = keyViewport();
    SDL_Event event{};
    event.key.windowID =
        static_cast<SDL_WindowID>(reinterpret_cast<uintptr_t>(viewport->PlatformHandle));
    event.key.scancode = codes[sender.tag];
    event.key.key = keys[sender.tag];
    event.key.mod = SDL_KMOD_GUI;
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.down = true;
    if (!SDL_PushEvent(&event)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not post text edit: %s", SDL_GetError());
        return;
    }
    event.type = SDL_EVENT_KEY_UP;
    event.key.down = false;
    event.key.mod = SDL_GetModState();
    if (!SDL_PushEvent(&event))
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not finish text edit: %s",
                     SDL_GetError());
}
@end

namespace lmx::app {

struct NativeMenuBar::Impl {
    LMXNativeMenuDelegate* delegate = [[LMXNativeMenuDelegate alloc] init];
    NSMenu* previous = NSApp.mainMenu;
    NSMenu* previousHelp = NSApp.helpMenu;
    NSMenu* previousWindows = NSApp.windowsMenu;
    LMXNativeSubmenu* menu = [[LMXNativeSubmenu alloc] initWithTitle:@"Luminex"];
};

//======================================================================================================================
NativeMenuBar::NativeMenuBar() : m_impl(std::make_unique<Impl>()) {}

//======================================================================================================================
std::unique_ptr<NativeMenuBar> NativeMenuBar::install() {
    auto owner = std::unique_ptr<NativeMenuBar>(new NativeMenuBar);
    auto& state = *owner->m_impl;
    state.menu->kind = -1;
    state.menu.delegate = state.delegate;
    state.menu.autoenablesItems = NO;
    NSArray* names = @[ @"Luminex", @"File", @"Edit", @"View", @"Window", @"Debug", @"Help" ];
    size_t modelIndex = 0;
    for (NSUInteger i = 0; i < names.count; ++i) {
        auto* submenu = [[LMXNativeSubmenu alloc] initWithTitle:names[i]];
        submenu->kind = i == 0 ? 1 : i == 2 ? 2 : 0;
        if (submenu->kind == 0)
            submenu->path = {modelIndex++};
        submenu.delegate = state.delegate;
        submenu.autoenablesItems = NO;
        [state.menu addItemWithTitle:names[i] action:nil keyEquivalent:@""].submenu = submenu;
    }
    NSApp.mainMenu = state.menu;
    NSApp.windowsMenu = [state.menu itemWithTitle:@"Window"].submenu;
    NSApp.helpMenu = state.menu.itemArray.lastObject.submenu;
    return owner;
}

//======================================================================================================================
NativeMenuBar::~NativeMenuBar() {
    if (NSApp.mainMenu == m_impl->menu) {
        NSApp.helpMenu = m_impl->previousHelp;
        NSApp.windowsMenu = m_impl->previousWindows;
        NSApp.mainMenu = m_impl->previous;
    }
}

//======================================================================================================================
void NativeMenuBar::update(std::vector<MenuItem> items, const ShortcutContext& context) {
    @autoreleasepool {
        auto* delegate = m_impl->delegate;
        delegate->model = std::move(items);
        delegate->context = context;
        for (NSMenuItem* row in m_impl->menu.itemArray) {
            auto* submenu = (LMXNativeSubmenu*)row.submenu;
            if (submenu->kind != 0 || submenu->path.front() >= delegate->model.size())
                continue;
            const auto& item = delegate->model[submenu->path.front()];
            row.enabled = item.enabled;
            row.toolTip = item.enabled ? nil : nativeString(item.disabledReason);
        }
        Appearance preference = Appearance::Auto;
        for (const auto mode : {Appearance::Auto, Appearance::Light, Appearance::Dark})
            if (const auto* item = findCommand(delegate->model, MenuCommand::Appearance,
                                               static_cast<uint32_t>(mode));
                item && item->checked)
                preference = mode;
        const auto forced = forcedWindowAppearance(preference);
        NSAppearance* appearance =
            forced ? [NSAppearance appearanceNamed:*forced == ThemeKind::Light
                                                       ? NSAppearanceNameAqua
                                                       : NSAppearanceNameDarkAqua]
                   : nil;
        if (m_impl->menu.appearance != appearance)
            m_impl->menu.appearance = appearance;
    }
}

//======================================================================================================================
std::vector<NativeMenuCommand> NativeMenuBar::takeCommands() {
    return takeCommands(nullptr);
}

//======================================================================================================================
std::vector<NativeMenuCommand> NativeMenuBar::takeCommands(const ShortcutContext* completedFrame) {
    auto& pending = m_impl->delegate->commands;
    auto& gui = *ImGui::GetCurrentContext();
    // SDL pumps native events before returning its queued events to the caller. Bind only after
    // that batch reaches the ImGui backend, using owned event IDs rather than key-state polling.
    for (auto& intent : pending) {
        if (intent.resolved || intent.inputEvent != 0)
            continue;
        ImGuiKeyChord modifiers = gui.IO.KeyMods;
        bool modifierMismatch = false;
        for (const auto& event : gui.InputEventsQueue) {
            if (event.Type == ImGuiInputEventType_Key && (event.Key.Key & ImGuiMod_Mask_)) {
                if (event.Key.Down)
                    modifiers |= event.Key.Key;
                else
                    modifiers &= ~event.Key.Key;
            }
            if (event.EventId < intent.firstEvent || event.Type != ImGuiInputEventType_Key ||
                !event.Key.Down || event.Key.Key != intent.key)
                continue;
            if (modifiers != intent.modifiers) {
                modifierMismatch = true;
                continue;
            }
            const bool assigned = std::ranges::any_of(
                pending, [&](const auto& other) { return other.inputEvent == event.EventId; });
            if (!assigned) {
                intent.inputEvent = event.EventId;
                break;
            }
        }
        if (intent.inputEvent == 0) {
            intent.resolved = true;
            intent.allowed = false;
            LMX_LOG_WARN("native shortcut dropped: {} ({})",
                         commandName(m_impl->delegate->model, intent.command),
                         modifierMismatch ? "modifier mismatch" : "no matching key event");
        }
    }
    if (completedFrame) {
        for (auto& intent : pending) {
            if (intent.resolved)
                continue;
            const bool consumed = std::ranges::any_of(gui.InputEventsTrail, [&](const auto& event) {
                return event.EventId == intent.inputEvent;
            });
            if (!consumed)
                continue;
            intent.resolved = true;
            auto focus = *completedFrame;
            // Tab and SetKeyboardFocusHere submit navigation whose activation applies next frame.
            // That navigation already owns this batch even before InputText acquires ActiveId.
            focus.textInput |=
                (gui.NavMoveSubmitted &&
                 (gui.NavMoveFlags & (ImGuiNavMoveFlags_IsTabbing | ImGuiNavMoveFlags_FocusApi))) ||
                gui.NavNextActivateId != 0;
            auto* viewport = keyViewport();
            focus.otherSurfaceFocused |=
                intent.otherSurfaceFocused ||
                (viewport && (viewport->Flags & ImGuiViewportFlags_NoAutoMerge));
            focus.cameraLook |= intent.cameraLook;
            const auto command = intent.command.first;
            if (command == MenuCommand::ZoomIn || command == MenuCommand::ZoomOut ||
                command == MenuCommand::ResetUiScale)
                focus.otherSurfaceFocused = false;
            auto decision =
                keyboardDecision(m_impl->delegate->model, command, intent.command.second, focus);
            const bool sameViewport = viewport && viewport->ID == intent.viewport;
            intent.allowed = sameViewport && !gui.IO.AppFocusLost &&
                             (decision.outcome == KeyboardOutcome::Run ||
                              decision.outcome == KeyboardOutcome::Report);
            intent.reason = std::move(decision.reason);
            if (!intent.allowed) {
                const auto outcome = decision.outcome;
                const char* cause = !sameViewport                              ? "viewport changed"
                                    : gui.IO.AppFocusLost                      ? "focus lost"
                                    : outcome == KeyboardOutcome::Focus        ? "focus"
                                    : outcome == KeyboardOutcome::OtherSurface ? "other surface"
                                    : outcome == KeyboardOutcome::Unmet ? "prerequisite unmet"
                                                                        : "disabled";
                // A chord that lost its viewport or application focus always warns; otherwise
                // the model decides, and plain typing into a field leaves no record.
                const bool commandChord = (intent.modifiers & ~ImGuiMod_Shift) != 0;
                const auto record = sameViewport && !gui.IO.AppFocusLost
                                        ? keyboardRecord(outcome, focus, commandChord)
                                        : KeyboardRecord::Warn;
                if (record == KeyboardRecord::Debug)
                    LMX_LOG_DEBUG("native shortcut dropped: {} ({})",
                                  commandName(m_impl->delegate->model, intent.command), cause);
                else if (record == KeyboardRecord::Warn)
                    LMX_LOG_WARN("native shortcut dropped: {} ({})",
                                 commandName(m_impl->delegate->model, intent.command), cause);
            }
        }
    }
    std::vector<NativeMenuCommand> result;
    size_t count = 0;
    while (count < pending.size() && pending[count].resolved) {
        auto& ready = pending[count];
        if (ready.allowed)
            result.push_back({ready.command.first, ready.command.second, std::move(ready.reason)});
        ++count;
    }
    pending.erase(pending.begin(), pending.begin() + count);
    return result;
}

} // namespace lmx::app
