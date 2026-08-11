//----------------------------------------------------------------------------------------------------------------------
/// @file GraphDump.h
/// @brief Declares the deterministic text dump of a compiled render-graph frame.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Render/RenderGraph.h"

#include <string>

namespace lmx::render {

/// Renders a compiled frame as text, deterministically.
///
/// The dump is a pure function of the record: the same declarations produce the same bytes, on any
/// machine and in any run. That is what makes it a golden file rather than a log, and it is why the
/// record it reads carries no GPU timing and no driver-reported value -- those belong to an
/// observer joining them to the frame, never to the frame's own description of itself.
///
/// Ordering is fixed at every level, since a dump whose lines moved would compare as changed
/// without anything having changed: resources in import order, sinks in declaration order,
/// scheduled passes in schedule order, culled passes after them in declaration order with their
/// reasons, each pass's uses in declaration order, and transitions in the order they are emitted.
/// Resources are named by index (`r0`) and passes by declaration index (`p0`), so a scheduled
/// section running `p1 p0` is itself readable as the reordering the compiler proved.
std::string dumpCompiledFrame(const CompiledFrameRecord& record);

/// Writes one frame's dump to the absolute path the `LMX_GRAPH_DUMP` environment variable names,
/// and logs where it went.
///
/// It writes the *first* record it is offered and nothing after it: a frame loop compiles a frame
/// every 16 milliseconds, so a trigger that rewrote the file each time would be a file whose
/// contents depend on when it was read. Unset the variable and it does nothing at all. RenderGraph
/// calls this from execute(), which is the one point every declared frame passes through.
void dumpCompiledFrameIfRequested(const CompiledFrameRecord& record);

} // namespace lmx::render
