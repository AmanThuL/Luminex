//----------------------------------------------------------------------------------------------------------------------
/// @file RhiReference.h
/// @brief Declares the untimed production graph and image reference.
//----------------------------------------------------------------------------------------------------------------------
#pragma once
#include "Model/Workload.h"

#include <filesystem>

namespace lmx::experimental::submission {

/// Executes one untimed offscreen frame through the public RHI and production RenderGraph.
/// Loads Scene and Prepare libraries from shaderDirectory, waits for retirement, and returns
/// tightly packed linear RGBA8 pixels, visible IDs, and the executed graph's dump.
/// Indirect visibility is decoded from retired argument records, with the batched route's packed
/// IDs also copied back after retirement; direct returns the IDs submitted by its draw loop.
/// GPU-argument visibility never comes from the CPU oracle.
/// Batched uses CPU-written instanced indirect arguments because the public RHI has no direct
/// instancing command; this substitution is confined to this correctness oracle. ICB is
/// unsupported. Owns all GPU resources for this call and destroys them only after submitted work
/// completes. Returns model, shader, allocation, graph-validation, or generated-argument failures
/// as text.
Result<FrameImage> renderReferenceFrame(const Case& spec, Suite suite, Variant variant,
                                        uint32_t logicalFrame,
                                        const std::filesystem::path& shaderDirectory);

} // namespace lmx::experimental::submission
