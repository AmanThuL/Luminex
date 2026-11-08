//----------------------------------------------------------------------------------------------------------------------
/// @file EditorSurfaces.h
/// @brief Declares SDL-owned native surfaces for the editor's platform viewports.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <rojoRHI/ImGuiAdapter.h>

namespace lmx::app {

/// Returns the main-thread callbacks that own each detached viewport's SDL Metal view.
/// The adapter releases its swapchain and waits for its window before destroying the surface.
rojoRHI::imgui::SurfaceCallbacks editorSurfaceCallbacks();

} // namespace lmx::app
