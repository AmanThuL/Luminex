#pragma once
#include <filesystem>

namespace lmx::app {

// `App --screenshot <out.bmp>`: renders one frame of the default scene offscreen -- no window, no
// swapchain, no ImGui -- and writes it out as an uncompressed 32-bit BMP. Returns a process exit
// code: 0 only when the frame rendered, both pixel probes held, and the file was written.
//
// Doubles as the headless sanity path for the whole render stack. Everything except presentation
// and the UI pass is the same code the editor runs, on the same scene (EditorShell.h).
int runScreenshot(const std::filesystem::path& outPath);

} // namespace lmx::app
