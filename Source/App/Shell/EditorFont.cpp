//----------------------------------------------------------------------------------------------------------------------
/// @file EditorFont.cpp
/// @brief Loads the editor's proportional typeface with fixed-width diagnostic digits.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Shell/EditorFont.h"

#include "App/Model/Workspace/EditorTheme.h"
#include "Core/Diagnostics/Log.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <filesystem>

namespace lmx::app {
namespace {

//======================================================================================================================
ImFont* loadSans(ImFontAtlas& atlas, const std::filesystem::path& path) {
    static constexpr ImWchar kDigits[] = {'0', '9', 0};
    static constexpr ImWchar kExceptDigits[] = {1, '0' - 1, '9' + 1, IM_UNICODE_CODEPOINT_MAX, 0};
    const float size = typeSpec(TypeRole::Body).size;
    ImFontConfig config;
    config.Flags = ImFontFlags_NoLoadError;
    config.GlyphExcludeRanges = kDigits;
    auto* face = atlas.AddFontFromFileTTF(path.c_str(), size, &config);
    if (!face)
        return nullptr;
    config.MergeMode = true;
    config.GlyphExcludeRanges = kExceptDigits;
    config.GlyphMinAdvanceX = kDigitAdvanceEm * size;
    config.GlyphMaxAdvanceX = kDigitAdvanceEm * size;
    atlas.AddFontFromFileTTF(path.c_str(), size, &config);
    return face;
}

//======================================================================================================================
bool mergeIcons(ImFontAtlas& atlas, ImFont* face, const std::filesystem::path& path) {
    static constexpr ImWchar kIcons[] = {0xEA60, 0xEC40, 0};
    static constexpr ImWchar kExceptIcons[] = {1, 0xEA5F, 0xEC41, IM_UNICODE_CODEPOINT_MAX, 0};
    const float size = typeSpec(TypeRole::Body).size;
    ImFontConfig icons;
    icons.Flags = ImFontFlags_NoLoadError;
    icons.MergeMode = true;
    icons.DstFont = face;
    icons.GlyphMinAdvanceX = size;
    icons.GlyphMaxAdvanceX = size;
    icons.GlyphExcludeRanges = kExceptIcons;
    return atlas.AddFontFromFileTTF(path.c_str(), size, &icons, kIcons) != nullptr;
}

//======================================================================================================================
void warnMissing(const std::filesystem::path& path, const char* fallback, bool& warned) {
    if (warned)
        return;
    warned = true;
    LMX_LOG_WARN("Editor font unavailable at '{}'; using {}. Run xmake setup and rebuild App "
                 "to restore Geist.",
                 path.string(), fallback);
}

} // namespace

//======================================================================================================================
EditorFonts configureEditorFonts() {
    static bool warnedRegular = false, warnedMedium = false, warnedMono = false,
                warnedIcons = false;
    auto& io = ImGui::GetIO();
    const char* basePath = SDL_GetBasePath();
    const auto directory = std::filesystem::path(basePath ? basePath : "") / "Fonts";
    EditorFonts fonts;
    fonts.sans = loadSans(*io.Fonts, directory / "Geist-Regular.ttf");
    if (!fonts.sans) {
        ImFontConfig fallback;
        fallback.SizePixels = typeSpec(TypeRole::Body).size;
        fonts.sans = io.Fonts->AddFontDefault(&fallback);
        warnMissing(directory / "Geist-Regular.ttf", "embedded fallback", warnedRegular);
    }
    io.FontDefault = fonts.sans;
    fonts.sansMedium = loadSans(*io.Fonts, directory / "Geist-Medium.ttf");
    if (!fonts.sansMedium) {
        fonts.sansMedium = fonts.sans;
        warnMissing(directory / "Geist-Medium.ttf", "Sans", warnedMedium);
    }
    ImFontConfig mono;
    mono.Flags = ImFontFlags_NoLoadError;
    fonts.mono = io.Fonts->AddFontFromFileTTF((directory / "GeistMono-Regular.ttf").c_str(),
                                              typeSpec(TypeRole::MonoBody).size, &mono);
    if (!fonts.mono) {
        fonts.mono = fonts.sans;
        warnMissing(directory / "GeistMono-Regular.ttf", "Sans", warnedMono);
    }
    const auto iconPath = directory / "codicon.ttf";
    fonts.icons = mergeIcons(*io.Fonts, fonts.sans, iconPath);
    if (fonts.icons && fonts.sansMedium != fonts.sans)
        fonts.icons = mergeIcons(*io.Fonts, fonts.sansMedium, iconPath);
    if (!fonts.icons && !warnedIcons) {
        warnedIcons = true;
        LMX_LOG_WARN("Editor icons unavailable at '{}'; using text labels. Run xmake setup "
                     "and rebuild App to restore Codicons.",
                     iconPath.string());
    }
    LMX_LOG_INFO("Editor fonts: {}, {}, {}; body {} logical points, Sans digits {} em; Codicons {}",
                 fonts.sans->GetDebugName(), fonts.sansMedium->GetDebugName(),
                 fonts.mono->GetDebugName(), typeSpec(TypeRole::Body).size, kDigitAdvanceEm,
                 fonts.icons ? "16 logical points" : "unavailable");
    return fonts;
}

} // namespace lmx::app
