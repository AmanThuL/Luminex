#pragma once
#include "RHI/RHI.h"

#include <string_view>

// Developer tooling, not part of the RHI surface: a Metal 4 GPU capture written as a .gputrace
// document that Xcode can open. Deliberately metal-cpp-free so the App target -- which does not
// have the metal-cpp include directory -- can call it.

namespace lmx::rhi::metal4 {

// Opens a capture that records everything encoded and committed until endCapture().
//
// The plan calls this `triggerCapture`; it is split in two because a capture has to *span* a
// frame. The frame being recorded does not exist yet when the request is made (a keypress, an
// automation hook), so a single call could only ever record zero frames.
//
// `outPath` is resolved against the process CWD when relative, and an existing document at that
// path is removed first (Metal refuses to overwrite one) -- so `outPath` must be non-empty and
// end in ".gputrace" (checked before that removal happens); anything else is rejected without
// touching the filesystem. Returns false -- after logging why -- when capture is unavailable or
// `outPath` fails that check. Metal capture itself is unavailable in the normal case: it only
// permits programmatic capture when `MTL_CAPTURE_ENABLED=1` is in the environment at launch. A
// false return means nothing was started, so the caller must not call endCapture().
//
// Process-global, like MTLCaptureManager itself: only one capture can be open at a time, and the
// path being written is remembered between the two calls.
bool beginCapture(Device& device, std::string_view outPath);

// Closes the capture beginCapture() opened and logs the document that was written. No-op when no
// capture is running. The caller is responsible for the GPU having finished the work it wants in
// the document (Device::waitIdle) before calling this.
void endCapture();

} // namespace lmx::rhi::metal4
