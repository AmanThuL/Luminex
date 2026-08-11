//----------------------------------------------------------------------------------------------------------------------
/// @file Types.h
/// @brief Declares the API-neutral resource, pass, and sink vocabulary the M5.1 workload manifest
///        is expressed in.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include <cstdint>
#include <string>
#include <vector>

/// The M5.1 workload manifest (spec sections 6-7): an API-neutral description of the frozen scored
/// core, shared by both the maintained-RHI and prototype adapters. Nothing here includes `RHI/` or
/// the prototype's `Experiments/NoApi/Include` headers -- the whole point of this library is that
/// neither encoder's headers has to appear in the other's dependency graph for the manifest itself
/// to compile (spec section 5).
namespace lmx::noapi::workload {

/// Pixel formats the manifest names. This mirrors the subset of `rhi::Format`
/// (`RHI/Include/RHI/RHI.h`) the scored core needs, restated here rather than included so this
/// library stays free of any RHI dependency; an adapter maps each enumerator to its own type.
enum class Format {
    RGBA8Unorm,
    RGBA8Unorm_sRGB,
    RGBA16Float,
    RG16Float,
    D32Float,
};

/// A texture resource the manifest declares, on `rhi::TextureDesc`'s terms restated without an RHI
/// dependency. `mipLevelCount == 0` in a use below means "the whole chain", the manifest's stand-in
/// for `rhi::kAllMipLevels`.
struct TextureResource {
    std::string id;      ///< Stable resource id, e.g. "R1" or "material0.emissive".
    std::string name;     ///< Human-readable name, e.g. "shadow".
    uint32_t width = 0;   ///< Extent in texels.
    uint32_t height = 0;  ///< Extent in texels.
    Format format = Format::RGBA8Unorm; ///< Pixel format.
    uint32_t mipLevels = 1;             ///< Allocated mip levels.
    bool renderTarget = false;  ///< Enables colour render-target use.
    bool depthTarget = false;   ///< Enables depth render-target use.
    bool sampled = false;       ///< Enables ordinary shader reads.
    bool storageRead = false;   ///< Enables storage-binding reads.
    bool storageWrite = false;  ///< Enables storage-binding writes.
    bool copySource = false;    ///< Enables use as a copy source.
    bool copyDestination = false; ///< Enables use as a copy destination.
    bool cpuReadback = false;   ///< Enables CPU readback.
    /// Whether the resource survives past this frame (only R5 `exposure`, spec section 6).
    bool persistent = false;
};

/// The buffer counterpart of TextureResource, on the same terms.
struct BufferResource {
    std::string id;    ///< Stable resource id, e.g. "R4".
    std::string name;   ///< Human-readable name, e.g. "histogram".
    uint64_t size = 0;  ///< Allocation size in bytes.
    bool storageRead = false;     ///< Enables storage-binding reads.
    bool storageWrite = false;    ///< Enables storage-binding writes.
    bool copySource = false;      ///< Enables use as a copy source.
    bool copyDestination = false; ///< Enables use as a copy destination or fill target.
    bool cpuReadback = false;     ///< Enables CPU readback.
    bool persistent = false;      ///< Whether the resource survives past this frame.
};

/// A subresource range on a TextureResource. `mipLevelCount == 0` names the whole chain from
/// `baseMipLevel`, the manifest's neutral stand-in for `rhi::kAllMipLevels`.
struct SubresourceRange {
    uint32_t baseMipLevel = 0;
    uint32_t mipLevelCount = 0;

    friend bool operator==(const SubresourceRange&, const SubresourceRange&) = default;
};

/// What kind of RHI pass scope a declared pass encodes into, on `lmx::render::PassKind`'s terms.
enum class PassKind { Raster, Compute, Copy };

/// What a pass does with one declared resource use, on `lmx::render::UseRole`'s terms restated for
/// both texture and buffer uses in one enumeration.
enum class UseRole {
    Read,             ///< An ordinary (non-shader) read: a copy source or an attachment load.
    ShaderRead,        ///< A sampled/SRV read from within a compute pass.
    Write,             ///< A non-attachment write.
    ColorAttachment,   ///< Written as a raster pass's colour attachment.
    DepthAttachment,   ///< Written as a raster pass's depth attachment.
    CopySource,        ///< Read by a copy command.
    CopyDestination,   ///< Written by a copy command.
};

/// One declared use of one texture or buffer resource by one pass.
struct ResourceUse {
    std::string resourceId;             ///< Id of the TextureResource or BufferResource used.
    UseRole role = UseRole::Read;       ///< What the pass does with it.
    SubresourceRange range;             ///< Subresources touched; ignored for buffers.
};

/// One declared pass, on `lmx::render::RenderGraph::addPass`/`addComputePass`/`addCopyPass`'s
/// terms. `uses` lists every resource the pass touches, in the order an adapter must declare them
/// for the derived schedule and barriers to match `RepresentativeGraph::expectedScheduleOrder`.
struct PassDeclaration {
    std::string id;     ///< Stable pass id, e.g. "P04".
    std::string label;   ///< Pass label, matching the production `lmx.pass.*` naming where shared.
    PassKind kind = PassKind::Raster; ///< Which RHI pass scope the pass encodes into.
    std::vector<ResourceUse> uses;    ///< Every resource version the pass touches, in order.
};

/// How a rooted result leaves the frame, on `lmx::render::SinkKind`'s terms (minus `Present`,
/// which the offscreen manifest never uses).
enum class SinkKind { Export, Readback };

/// One declared sink: a resource id and how it leaves the frame.
struct SinkDeclaration {
    std::string resourceId;
    SinkKind kind = SinkKind::Export;
};

} // namespace lmx::noapi::workload
