//----------------------------------------------------------------------------------------------------------------------
/// @file Handles.h
/// @brief Declares the opaque object types the prototype interface still needs.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

namespace lmx::experimental::noapi {

/// Owns the connection to one GPU and every object created from it.
///
/// Created by `createDevice` and destroyed by `destroyDevice`. Every other creation entry point
/// takes the owning device, and every destruction entry point takes it back; the prototype keeps
/// no global device state. Not thread-safe: creation and destruction are single-threaded.
struct Device;

/// Accepts submitted command buffers in submission order.
///
/// Obtained from `mainQueue`; owned by the device and never destroyed by the caller. The prototype
/// exposes exactly one queue, because multi-queue work is out of the experiment's scope.
struct Queue;

/// Records commands for one submission and is consumed by that submission.
///
/// Command buffers are transient by contract: `beginCommands` produces one, `submit` consumes it,
/// and it is invalid afterwards. There is no reset, no reuse, and no secondary command buffer.
struct CommandBuffer;

/// Holds the driver-side texture object that the rasterizer and copy engines still require.
///
/// Textures are the one resource kind that survives as an object, because render pass attachments,
/// copies, and clears are not bindless on any current hardware. Shader access never goes through
/// this object; it goes through a bindless table slot written from it.
struct Texture;

/// Holds an immutable texture filtering and addressing state.
///
/// Samplers exist as objects only so their handle can be written into a bindless table slot.
struct Sampler;

/// Holds one flat array of shader-visible texture and sampler handles.
///
/// The prototype creates exactly one table per device. Its address is a plain `GpuAddress`, so
/// shaders index it directly and callers may store a slot index in any structure they own.
struct BindlessTable;

/// Holds a compiled compute or graphics pipeline.
///
/// Pipelines carry no binding layout: root data is an address supplied per draw or dispatch, and
/// texture access is a table slot index. What a pipeline still bakes is the shader microcode plus
/// the state that changes it — attachment formats, sample count, topology, and any embedded blend
/// state.
struct Pipeline;

/// Holds an immutable depth and stencil test configuration.
///
/// Separate from the pipeline so that changing the depth test does not recompile microcode.
struct DepthStencilState;

/// Holds a monotonically increasing counter used for GPU-to-CPU frame pacing.
///
/// Split barriers inside a command buffer do not use this object; they signal and wait on plain
/// GPU memory. This object exists only where the CPU must observe GPU progress.
struct Semaphore;

/// Holds the single set of allocations the GPU may access during a submission.
///
/// The comparison model has no residency concept at all — allocations are simply memory. The
/// prototype keeps exactly one set because the target API requires residency to be declared, and
/// one set is the smallest honest expression of that requirement.
struct ResidencySet;

} // namespace lmx::experimental::noapi
