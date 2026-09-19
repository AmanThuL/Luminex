//----------------------------------------------------------------------------------------------------------------------
/// @file RenderGraphValidation.cpp
/// @brief Validates graph attachment, subresource and transient declarations.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/RenderGraph.h"
#include "Render/RenderGraphInternal.h"

#include <algorithm>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>

namespace lmx::render {
using graph_detail::fail;
using graph_detail::rangesOverlap;
using graph_detail::resolveRange;
using graph_detail::sinkVerb;

namespace {

//======================================================================================================================
bool isDepthFormat(rojoRHI::Format format) {
    return format == rojoRHI::Format::D32Float;
}

//======================================================================================================================
// The colour attachment role, on the same terms the RHI's own desc validation uses: 8-bit unorm
// targets, sRGB included, the single-channel unorm a mask target carries, plus the two float
// formats -- the half-float scene colour and the two-channel half-float a motion-vector target
// carries. It has to answer exactly as the RHI's predicate does, or the graph refuses a target
// the device would have accepted. A block-compressed format cannot be rendered into, and a depth
// format belongs in the other slot.
bool isColorRenderableFormat(rojoRHI::Format format) {
    switch (format) {
    case rojoRHI::Format::BGRA8Unorm:
    case rojoRHI::Format::RGBA8Unorm:
    case rojoRHI::Format::RGBA8Unorm_sRGB:
    case rojoRHI::Format::RGBA16Float:
    case rojoRHI::Format::RG16Float:
    case rojoRHI::Format::R8Unorm:
        return true;
    case rojoRHI::Format::R16Float:
    case rojoRHI::Format::R32Float:
    case rojoRHI::Format::Unknown:
    case rojoRHI::Format::BC1Unorm:
    case rojoRHI::Format::BC1Unorm_sRGB:
    case rojoRHI::Format::D32Float:
        return false;
    }
    return false;
}

//======================================================================================================================
bool validAxis(uint32_t base, uint32_t count, uint32_t available, uint32_t allSentinel) {
    if (base >= available) {
        return false;
    }
    return count == allSentinel || (count > 0 && count <= available - base);
}

//======================================================================================================================
// A range covers real subresources only when both axes do, so an empty count on either one is an
// empty range whatever the other says.
bool isEmptyRange(const rojoRHI::TextureSubresourceRange& range) {
    return range.mipLevelCount == 0 || range.arrayLayerCount == 0;
}

} // namespace

//======================================================================================================================
GraphResult<void> RenderGraph::validateExtraColorAttachments(const Pass& pass) const {
    if (pass.extraColor.empty()) {
        return {};
    }
    // Extras are attachments 1 and up: the count is what the hardware can bind past attachment
    // zero, and attachment zero has to be there for any of them to mean anything.
    if (pass.extraColor.size() > rojoRHI::kMaxExtraColorTargets) {
        return fail(std::format("pass '{}' declares {} extra color attachments, past the {} a "
                                "pass can bind beyond its primary one",
                                pass.label, pass.extraColor.size(), rojoRHI::kMaxExtraColorTargets));
    }
    if (!pass.color) {
        const Resource& first = m_resources[pass.extraColor.front().handle.index];
        return fail(std::format("pass '{}' declares extra color attachment 0 '{}' and no color "
                                "attachment: extras are attachments 1 and up, so a pass "
                                "without attachment zero cannot have them",
                                pass.label, first.name));
    }

    const Resource& primary = m_resources[pass.color->handle.index];
    for (uint32_t index = 0; index < pass.extraColor.size(); ++index) {
        const Resource& extra = m_resources[pass.extraColor[index].handle.index];
        if (!isColorRenderableFormat(extra.format)) {
            return fail(std::format("pass '{}' extra color attachment {} '{}' declares format "
                                    "{}, which is not color-renderable",
                                    pass.label, index, extra.name, formatName(extra.format)));
        }
        // One rasterisation writes every attachment, so an extra of another extent has
        // fragments with nowhere to land.
        if (extra.width != primary.width || extra.height != primary.height) {
            return fail(std::format("pass '{}' attachment extent mismatch: color '{}' is {}x{} "
                                    "and extra color attachment {} '{}' is {}x{}",
                                    pass.label, primary.name, primary.width, primary.height, index,
                                    extra.name, extra.width, extra.height));
        }
        // Two attachments over one texture would have the pass write those texels twice in one
        // rasterisation, and no version says what they then hold.
        const bool repeatsPrimary = pass.extraColor[index].handle.index == pass.color->handle.index;
        const auto repeated = std::ranges::find(
            pass.extraColor.begin(), pass.extraColor.begin() + index,
            pass.extraColor[index].handle.index,
            [](const ColorAttachment& attachment) { return attachment.handle.index; });
        if (repeatsPrimary || repeated != pass.extraColor.begin() + index) {
            return fail(std::format("pass '{}' declares texture '{}' as two of its color "
                                    "attachments: a pass writes each of its attachments once, "
                                    "so it may not name one texture twice",
                                    pass.label, extra.name));
        }
    }
    return {};
}

//======================================================================================================================
GraphResult<void> RenderGraph::validateRenderArea(const Pass& pass) const {
    const uint32_t width = pass.renderAreaWidth;
    const uint32_t height = pass.renderAreaHeight;
    // Zero is the whole attachment, which is what every pass that never asked for a sub-region
    // carries.
    if (width == 0 && height == 0) {
        return {};
    }
    // A half-set pair reaches the backend as a viewport one of whose sides is zero, rasterising
    // nothing at all rather than the sub-rectangle the pass meant.
    if (width == 0 || height == 0) {
        return fail(std::format("pass '{}' declares render area {}x{}: both sides are zero -- the "
                                "whole attachment -- or both are non-zero",
                                pass.label, width, height));
    }
    // Every attachment is rasterised by the same fragments, so the area has to fit inside all of
    // them; the extras and the depth target are checked beside the primary rather than through it
    // so the message names the attachment that is actually too small.
    const auto fits = [&](const Resource& attachment) -> GraphResult<void> {
        if (width > attachment.width || height > attachment.height) {
            return fail(std::format("pass '{}' render area {}x{} exceeds attachment '{}' {}x{}",
                                    pass.label, width, height, attachment.name, attachment.width,
                                    attachment.height));
        }
        return {};
    };
    if (pass.color) {
        if (const GraphResult<void> primary = fits(m_resources[pass.color->handle.index]);
            !primary) {
            return primary;
        }
    }
    for (const ColorAttachment& extra : pass.extraColor) {
        if (const GraphResult<void> fitsExtra = fits(m_resources[extra.handle.index]); !fitsExtra) {
            return fitsExtra;
        }
    }
    if (pass.depth) {
        if (const GraphResult<void> fitsDepth = fits(m_resources[pass.depth->handle.index]);
            !fitsDepth) {
            return fitsDepth;
        }
    }
    return {};
}

//======================================================================================================================
GraphResult<void> RenderGraph::validateDeclarations() const {
    // Attachment roles and extents. These depend on one pass alone, so they are answered before any
    // cross-pass structure is built and cannot be masked by an ordering failure.
    for (const Pass& pass : m_passes) {
        if (pass.color) {
            const Resource& color = m_resources[pass.color->handle.index];
            if (!isColorRenderableFormat(color.format)) {
                return fail(std::format("pass '{}' color attachment '{}' declares format {}, which "
                                        "is not color-renderable",
                                        pass.label, color.name, formatName(color.format)));
            }
        }
        if (pass.depth) {
            const Resource& depth = m_resources[pass.depth->handle.index];
            if (!isDepthFormat(depth.format)) {
                return fail(std::format("pass '{}' depth attachment '{}' declares format {}, which "
                                        "is not a depth format",
                                        pass.label, depth.name, formatName(depth.format)));
            }
        }
        if (pass.color && pass.depth) {
            const Resource& color = m_resources[pass.color->handle.index];
            const Resource& depth = m_resources[pass.depth->handle.index];
            if (color.width != depth.width || color.height != depth.height) {
                return fail(std::format("pass '{}' attachment extent mismatch: color '{}' is {}x{} "
                                        "and depth '{}' is {}x{}",
                                        pass.label, color.name, color.width, color.height,
                                        depth.name, depth.width, depth.height));
            }
        }
        if (const GraphResult<void> extras = validateExtraColorAttachments(pass); !extras) {
            return std::unexpected(extras.error());
        }
        if (const GraphResult<void> area = validateRenderArea(pass); !area) {
            return std::unexpected(area.error());
        }
    }

    // Subresource rules (spec §6). Both depend on one pass alone as well: a range that names no
    // subresource of its texture is a broken declaration whatever the schedule, and a pass reading
    // and writing one subresource has an intra-pass hazard no ordering between passes can fix.
    for (const Pass& pass : m_passes) {
        for (const Declaration& declaration : pass.declarations) {
            const Resource& resource = m_resources[declaration.resource];
            if (resource.kind != ResourceKind::Texture) {
                continue;
            }
            if (isEmptyRange(declaration.range)) {
                return fail(std::format("pass '{}' declares a {} of texture '{}' over {}, which "
                                        "covers no subresource",
                                        pass.label, roleName(declaration.role), resource.name,
                                        describeRange(declaration.range)));
            }
            const bool validMips =
                validAxis(declaration.range.baseMipLevel, declaration.range.mipLevelCount,
                          resource.mipLevels, rojoRHI::kAllMipLevels);
            const bool validLayers =
                validAxis(declaration.range.baseArrayLayer, declaration.range.arrayLayerCount,
                          resource.arrayLayers, rojoRHI::kAllArrayLayers);
            if (!validMips || !validLayers) {
                return fail(std::format("pass '{}' declares a {} of texture '{}' over {}, which "
                                        "runs past its {} mip levels and {} array layers",
                                        pass.label, roleName(declaration.role), resource.name,
                                        describeRange(declaration.range), resource.mipLevels,
                                        resource.arrayLayers));
            }
        }

        for (const Declaration& read : pass.declarations) {
            if (read.isWrite || m_resources[read.resource].kind != ResourceKind::Texture) {
                continue;
            }
            for (const Declaration& write : pass.declarations) {
                if (!write.isWrite || write.resource != read.resource) {
                    continue;
                }
                const Resource& resource = m_resources[read.resource];
                if (!rangesOverlap(
                        resolveRange(read.range, resource.mipLevels, resource.arrayLayers),
                        resolveRange(write.range, resource.mipLevels, resource.arrayLayers))) {
                    continue;
                }
                return fail(std::format(
                    "pass '{}' declares a {} of texture '{}' over {} and a {} of it over {}, which "
                    "overlap: one pass may read and write a texture only through disjoint ranges",
                    pass.label, roleName(read.role), resource.name, describeRange(read.range),
                    roleName(write.role), describeRange(write.range)));
            }
        }
    }

    // Transient rules (spec 11). Both are what leaves a pooled frame indistinguishable from an
    // unpooled one. A transient's version 0 is uninitialised memory rather than contents --
    // whatever the previous occupant of those bytes left -- so consuming it is refused rather than
    // allowed to depend on the packing; and a transient stops existing with the frame, so no sink
    // can name one.
    for (const Pass& pass : m_passes) {
        for (const Declaration& declaration : pass.declarations) {
            const Resource& resource = m_resources[declaration.resource];
            if (!resource.transient || declaration.version > 0 || declaration.isWrite) {
                continue;
            }
            return fail(std::format(
                "pass '{}' declares a {} of transient {} '{}' version 0, whose contents no pass "
                "produced: a transient holds nothing until a pass writes it",
                pass.label, roleName(declaration.role),
                resource.kind == ResourceKind::Texture ? "texture" : "buffer", resource.name));
        }
        // An attachment that loads consumes the version it names as well as writing it, which the
        // flattened declaration above records as a write and so cannot catch.
        const auto loadsTransient = [&](const std::optional<GraphTexture>& handle, LoadOp load,
                                        std::string_view role) -> std::optional<std::string> {
            if (!handle || load != LoadOp::Load) {
                return std::nullopt;
            }
            const Resource& resource = m_resources[handle->index];
            if (!resource.transient || handle->version > 0) {
                return std::nullopt;
            }
            return std::format("pass '{}' loads transient texture '{}' version 0 as its {}, whose "
                               "contents no pass produced: a transient holds nothing until a pass "
                               "writes it",
                               pass.label, resource.name, role);
        };
        if (const auto message =
                loadsTransient(pass.color ? std::optional{pass.color->handle} : std::nullopt,
                               pass.color ? pass.color->load : LoadOp::Clear, "color attachment")) {
            return fail(*message);
        }
        for (uint32_t index = 0; index < pass.extraColor.size(); ++index) {
            if (const auto message =
                    loadsTransient(pass.extraColor[index].handle, pass.extraColor[index].load,
                                   std::format("extra color attachment {}", index))) {
                return fail(*message);
            }
        }
        if (const auto message =
                loadsTransient(pass.depth ? std::optional{pass.depth->handle} : std::nullopt,
                               pass.depth ? pass.depth->load : LoadOp::Clear, "depth attachment")) {
            return fail(*message);
        }
    }

    for (const Sink& sink : m_sinks) {
        const Resource& resource = m_resources[sink.resource];
        if (!resource.transient) {
            continue;
        }
        return fail(std::format("{} {} '{}' is transient: a transient lives for exactly one frame, "
                                "so nothing outside that frame can read it -- import a resource of "
                                "your own for a result that has to survive",
                                sinkVerb(sink.kind),
                                resource.kind == ResourceKind::Texture ? "texture" : "buffer",
                                resource.name));
    }

    return {};
}

} // namespace lmx::render
