//----------------------------------------------------------------------------------------------------------------------
/// @file EditorFont.cpp
/// @brief Loads the editor's proportional typeface with fixed-width diagnostic digits.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Shell/EditorFont.h"

#include "Core/Diagnostics/Log.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <filesystem>

namespace lmx::app {

//======================================================================================================================
bool configureEditorFont() {
    constexpr float kReferenceSize = 16.0f;
    // The variable file's default outlines are Regular. ImGui scales these advances with the
    // requested size; no font-file modification or OpenType shaping is required for stable digits.
    constexpr float kDigitAdvance = 9.0f;
    static constexpr ImWchar kDigits[] = {'0', '9', 0};
    static constexpr ImWchar kExceptDigits[] = {1, '0' - 1, '9' + 1, IM_UNICODE_CODEPOINT_MAX, 0};
    ImGuiIO& io = ImGui::GetIO();
    const char* basePath = SDL_GetBasePath();
    const auto fontPath = basePath != nullptr
                              ? std::filesystem::path(basePath) / "Fonts/InterVariable.ttf"
                              : std::filesystem::path{};
    ImFontConfig config;
    config.Flags = ImFontFlags_NoLoadError;
    config.GlyphExcludeRanges = kDigits;
    ImFont* face = nullptr;
    if (!fontPath.empty()) {
        face = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), kReferenceSize, &config);
    }
    if (face != nullptr) {
        config.MergeMode = true;
        config.GlyphExcludeRanges = kExceptDigits;
        config.GlyphMinAdvanceX = kDigitAdvance;
        config.GlyphMaxAdvanceX = kDigitAdvance;
        face = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), kReferenceSize, &config);
    }
    if (face == nullptr) {
        io.Fonts->Clear();
        ImFontConfig fallback;
        fallback.SizePixels = kReferenceSize;
        io.FontDefault = io.Fonts->AddFontDefault(&fallback);
        LMX_LOG_WARN("Editor font unavailable at '{}'; using embedded fallback. Run xmake setup "
                     "and rebuild App to restore Inter.",
                     fontPath.string());
        return false;
    }
    io.FontDefault = face;
    LMX_LOG_INFO("Editor font: Inter Regular, {} logical points, tabular digits", kReferenceSize);
    const auto iconPath = std::filesystem::path(basePath) / "Fonts/codicon.ttf";
    static constexpr ImWchar kIcons[] = {0xEA60, 0xEC40, 0};
    // Exclusions also constrain the dynamic atlas, which does not use legacy glyph ranges.
    static constexpr ImWchar kExceptIcons[] = {1, 0xEA5F, 0xEC41, IM_UNICODE_CODEPOINT_MAX, 0};
    ImFontConfig icons;
    icons.Flags = ImFontFlags_NoLoadError;
    icons.MergeMode = true;
    icons.GlyphMinAdvanceX = kReferenceSize;
    icons.GlyphExcludeRanges = kExceptIcons;
    if (io.Fonts->AddFontFromFileTTF(iconPath.c_str(), kReferenceSize, &icons, kIcons) == nullptr) {
        LMX_LOG_WARN("Editor icons unavailable at '{}'; using text labels. Run xmake setup "
                     "and rebuild App to restore Codicons.",
                     iconPath.string());
        return false;
    }
    LMX_LOG_INFO("Editor icons: Codicons, {} logical points", kReferenceSize);
    return true;
}

} // namespace lmx::app
