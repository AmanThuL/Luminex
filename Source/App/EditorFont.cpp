//----------------------------------------------------------------------------------------------------------------------
/// @file EditorFont.cpp
/// @brief Loads the editor's proportional typeface with fixed-width diagnostic digits.
//----------------------------------------------------------------------------------------------------------------------

#include "App/EditorFont.h"

#include "Core/Log.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <filesystem>

namespace lmx::app {

//======================================================================================================================
void configureEditorFont() {
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
        return;
    }
    io.FontDefault = face;
    LMX_LOG_INFO("Editor font: Inter Regular, {} logical points, tabular digits", kReferenceSize);
}

} // namespace lmx::app
