//----------------------------------------------------------------------------------------------------------------------
/// @file RhiReference.cpp
/// @brief Executes the submission correctness oracle through public RHI graph passes.
//----------------------------------------------------------------------------------------------------------------------
#include "RhiReference.h"

#include "RHI/Indirect.h"
#include "Render/GraphDump.h"
#include "Render/RenderGraph.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>

namespace lmx::experimental::submission {
namespace {

constexpr uint32_t kPrepareThreads = 64;
constexpr uint64_t kRowBytes = uint64_t{kExtent} * 4;
constexpr uint64_t kImageBytes = kRowBytes * kExtent;
constexpr rhi::DrawIndirectArgs kUnwritten{0, 0, 0xFFFFFFFFu, 0xFFFFFFFFu};

static_assert(sizeof(Params) == 192 && offsetof(Params, count) == 160);
static_assert(offsetof(Params, batched) == 176 && offsetof(Params, epsilon) == 188);
static_assert(sizeof(Instance) == 32 && offsetof(Instance, bin) == 20);
static_assert(sizeof(rhi::DrawIndirectArgs) == 16);

//======================================================================================================================
template <typename T>
Result<T> translate(rhi::Result<T> result, std::string_view operation) {
    if (!result) {
        return std::unexpected(std::string(operation) + ": " + result.error().message);
    }
    return std::move(*result);
}

//======================================================================================================================
template <typename T>
Result<std::unique_ptr<rhi::Buffer>> upload(rhi::Device& device, std::span<const T> values,
                                            std::string_view label, bool storageWrite = false) {
    // Empty inputs still need a valid binding, but no shader may address the dummy element.
    const T dummy{};
    return translate(
        device.createBuffer({.size = std::max<uint64_t>(values.size_bytes(), sizeof(T)),
                             .storageWrite = storageWrite,
                             .label = label},
                            values.empty() ? &dummy : values.data()),
        label);
}

//======================================================================================================================
bool sameArgs(const rhi::DrawIndirectArgs& a, const rhi::DrawIndirectArgs& b) {
    return a.vertexCount == b.vertexCount && a.instanceCount == b.instanceCount &&
           a.firstVertex == b.firstVertex && a.firstInstance == b.firstInstance;
}

} // namespace

//======================================================================================================================
Result<FrameImage> renderReferenceFrame(const Case& spec, Suite suite, Variant variant,
                                        uint32_t logicalFrame,
                                        const std::filesystem::path& shaderDirectory) {
    if (variant == Variant::GpuIcb) {
        return std::unexpected("The production RHI reference does not expose ICB execution");
    }
    if (variant != Variant::Direct && variant != Variant::CpuIndirect &&
        variant != Variant::GpuArgs && variant != Variant::Batched) {
        return std::unexpected("Unknown reference submission variant");
    }
    if (suite != Suite::S && suite != Suite::E) {
        return std::unexpected("Unknown reference submission suite");
    }
    auto input = makeFrame(spec, logicalFrame);
    if (!input) {
        return std::unexpected(input.error());
    }
    auto& frame = *input;
    const bool gpuArgs = variant == Variant::GpuArgs;
    const bool batched = variant == Variant::Batched;
    const bool indirect = variant != Variant::Direct;
    const uint32_t vertices = 3 * spec.triangles;
    frame.params.suite = suite == Suite::S ? 0 : 1;
    frame.params.batched = batched ? 1 : 0;
    frame.params.binOffset = 0;
    frame.params.debug = 0;

    std::vector<uint32_t> visibleIds;
    if (!gpuArgs) {
        if (suite == Suite::E) {
            visibleIds = classify(frame);
        } else {
            for (uint32_t id = 0; id < spec.count; ++id) {
                if (frame.bitmap[id] != 0) {
                    visibleIds.push_back(id);
                }
            }
        }
        std::stable_sort(visibleIds.begin(), visibleIds.end(), [&](uint32_t a, uint32_t b) {
            return frame.instances[a].bin < frame.instances[b].bin;
        });
    }
    std::vector<uint32_t> binOffsets(spec.bins + 1, 0);
    for (uint32_t id : visibleIds) {
        ++binOffsets[frame.instances[id].bin + 1];
    }
    for (uint32_t bin = 0; bin < spec.bins; ++bin) {
        binOffsets[bin + 1] += binOffsets[bin];
    }

    std::vector<std::array<float, 4>> colors(spec.bins);
    for (uint32_t bin = 0; bin < spec.bins; ++bin) {
        colors[bin] = {(bin * 37 % 251 + 1) / 252.f, (bin * 73 % 251 + 1) / 252.f,
                       (bin * 109 % 251 + 1) / 252.f, 1.f};
    }
    std::vector<rhi::DrawIndirectArgs> arguments;
    if (gpuArgs) {
        // The extra record catches dispatch-tail writes. Zero counts keep unwritten slots inert.
        arguments.assign(size_t{spec.count} + 1, kUnwritten);
    } else if (batched) {
        for (uint32_t bin = 0; bin < spec.bins; ++bin) {
            arguments.push_back({vertices, binOffsets[bin + 1] - binOffsets[bin], 0, 0});
        }
    } else if (indirect) {
        for (uint32_t id : visibleIds) {
            arguments.push_back({vertices, 1, id * vertices, 0});
        }
    }

    auto deviceResult = translate(rhi::createDevice(), "Create reference device");
    if (!deviceResult) {
        return std::unexpected(deviceResult.error());
    }
    auto device = std::move(*deviceResult);
    auto scene = translate(device->loadShaderLibrary((shaderDirectory / "Scene").string()),
                           "Load reference Scene");
    if (!scene) {
        return std::unexpected(scene.error());
    }
    auto pipeline =
        translate(device->createGraphicsPipeline({.library = scene->get(),
                                                  .vertexEntry = "vertexMain",
                                                  .fragmentEntry = "fragmentMain",
                                                  .colorFormat = rhi::Format::RGBA8Unorm,
                                                  .depthFormat = rhi::Format::D32Float,
                                                  .depthTestEnable = true,
                                                  .depthWriteEnable = true,
                                                  .cullMode = rhi::CullMode::None,
                                                  .depthCompare = rhi::DepthCompare::Greater,
                                                  .label = "submission.reference.scenePipeline"}),
                  "Create reference scene pipeline");
    if (!pipeline) {
        return std::unexpected(pipeline.error());
    }
    std::unique_ptr<rhi::ShaderLibrary> prepare;
    std::unique_ptr<rhi::ComputePipeline> preparePipeline;
    if (gpuArgs) {
        auto library = translate(device->loadShaderLibrary((shaderDirectory / "Prepare").string()),
                                 "Load reference Prepare");
        if (!library) {
            return std::unexpected(library.error());
        }
        prepare = std::move(*library);
        auto compute = translate(
            device->createComputePipeline({.library = prepare.get(),
                                           .computeEntry = "computeMain",
                                           .threadsPerThreadgroup = {kPrepareThreads, 1, 1},
                                           .label = "submission.reference.preparePipeline"}),
            "Create reference preparation pipeline");
        if (!compute) {
            return std::unexpected(compute.error());
        }
        preparePipeline = std::move(*compute);
    }

    auto instances = upload<Instance>(*device, frame.instances, "submission.reference.instances");
    if (!instances) {
        return std::unexpected(instances.error());
    }
    // E has no CPU visibility input, including in this untimed replay of the scored kernel.
    const std::vector<uint32_t> bitmap = suite == Suite::S ? frame.bitmap : std::vector<uint32_t>{};
    auto visibility = upload<uint32_t>(*device, bitmap, "submission.reference.visibility");
    if (!visibility) {
        return std::unexpected(visibility.error());
    }
    auto ids = upload<uint32_t>(
        *device, batched ? std::span<const uint32_t>{visibleIds} : std::span<const uint32_t>{},
        "submission.reference.visibleIds");
    if (!ids) {
        return std::unexpected(ids.error());
    }
    auto colorTable = upload<std::array<float, 4>>(*device, colors, "submission.reference.colors");
    if (!colorTable) {
        return std::unexpected(colorTable.error());
    }
    auto args = upload<rhi::DrawIndirectArgs>(*device, arguments, "submission.reference.arguments",
                                              gpuArgs);
    if (!args) {
        return std::unexpected(args.error());
    }
    std::vector<std::unique_ptr<rhi::Buffer>> params;
    for (uint32_t bin = 0; bin < (batched ? spec.bins : 1); ++bin) {
        Params value = frame.params;
        value.binOffset = batched ? binOffsets[bin] : 0;
        auto buffer = upload<Params>(*device, std::span{&value, 1}, "submission.reference.params");
        if (!buffer) {
            return std::unexpected(buffer.error());
        }
        params.push_back(std::move(*buffer));
    }
    auto target = translate(device->createTexture({.width = kExtent,
                                                   .height = kExtent,
                                                   .format = rhi::Format::RGBA8Unorm,
                                                   .renderTarget = true,
                                                   .label = "submission.reference.color"}),
                            "Create reference color target");
    if (!target) {
        return std::unexpected(target.error());
    }
    auto depth = translate(device->createTexture({.width = kExtent,
                                                  .height = kExtent,
                                                  .format = rhi::Format::D32Float,
                                                  .renderTarget = true,
                                                  .label = "submission.reference.depth"}),
                           "Create reference depth target");
    if (!depth) {
        return std::unexpected(depth.error());
    }
    auto pixelReadback =
        translate(device->createBuffer({.size = kImageBytes,
                                        .cpuReadback = true,
                                        .label = "submission.reference.pixelReadback"},
                                       nullptr),
                  "Create reference pixel readback");
    if (!pixelReadback) {
        return std::unexpected(pixelReadback.error());
    }
    auto argumentReadback =
        translate(device->createBuffer({.size = (*args)->size(),
                                        .cpuReadback = true,
                                        .label = "submission.reference.argumentReadback"},
                                       nullptr),
                  "Create reference argument readback");
    if (!argumentReadback) {
        return std::unexpected(argumentReadback.error());
    }
    std::unique_ptr<rhi::Buffer> idsReadback;
    if (batched) {
        auto buffer = translate(device->createBuffer({.size = (*ids)->size(),
                                                      .cpuReadback = true,
                                                      .label = "submission.reference.idsReadback"},
                                                     nullptr),
                                "Create reference visible-ID readback");
        if (!buffer) {
            return std::unexpected(buffer.error());
        }
        idsReadback = std::move(*buffer);
    }

    render::RenderGraph graph;
    const auto instanceHandle = graph.importBuffer(**instances, "instances");
    const auto visibilityHandle =
        graph.importBuffer(**visibility, suite == Suite::S ? "visibility" : "visibilityDummy");
    const auto idsHandle = graph.importBuffer(**ids, "visibleIds");
    const auto colorsHandle = graph.importBuffer(**colorTable, "colors");
    auto argsHandle = graph.importBuffer(**args, "arguments");
    std::vector<render::GraphBuffer> paramHandles;
    for (const auto& buffer : params) {
        paramHandles.push_back(graph.importBuffer(*buffer, "params"));
    }
    const auto colorHandle = graph.importTexture(**target, rhi::Format::RGBA8Unorm, "color");
    const auto depthHandle = graph.importTexture(**depth, rhi::Format::D32Float, "depth");
    const auto pixelsHandle = graph.importBuffer(**pixelReadback, "pixelReadback");
    const auto readArgsHandle = graph.importBuffer(**argumentReadback, "argumentReadback");
    render::GraphBuffer readIdsHandle;
    if (batched) {
        readIdsHandle = graph.importBuffer(*idsReadback, "idsReadback");
    }
    rhi::CommandList* commands = nullptr;
    if (gpuArgs && spec.count != 0) {
        render::ComputePassDesc desc;
        desc.shaderBufferReads = {paramHandles.front(), instanceHandle, visibilityHandle};
        desc.bufferWrites = {argsHandle};
        graph.addComputePass("submission.reference.prepare", std::move(desc),
                             [&, output = argsHandle](const render::PassResources& resources) {
                                 commands->bindComputePipeline(*preparePipeline);
                                 commands->bindBuffer(0, **resources.buffer(paramHandles.front()));
                                 commands->bindBuffer(1, **resources.buffer(instanceHandle));
                                 // Metal validation requires every declared binding even when
                                 // the uniform suite branch skips it. E binds a zero dummy only.
                                 commands->bindBuffer(2, **resources.buffer(visibilityHandle));
                                 commands->bindStorageBuffer(3, **resources.buffer(output),
                                                             rhi::StorageAccess::Write);
                                 commands->dispatch(
                                     (spec.count + kPrepareThreads - 1) / kPrepareThreads, 1, 1);
                             });
        argsHandle = render::nextVersion(argsHandle);
    }

    render::PassDesc raster;
    raster.bufferReads = {instanceHandle, colorsHandle, idsHandle};
    raster.bufferReads.insert(raster.bufferReads.end(), paramHandles.begin(), paramHandles.end());
    if (indirect) {
        raster.indirectBufferReads = {argsHandle};
    }
    raster.color = render::ColorAttachment{.handle = colorHandle};
    raster.depth = render::DepthAttachment{.handle = depthHandle};
    graph.addPass("submission.reference.raster", std::move(raster),
                  [&, drawArgs = argsHandle](const render::PassResources& resources) {
                      commands->bindPipeline(**pipeline);
                      commands->bindBuffer(0, **resources.buffer(paramHandles.front()));
                      commands->bindBuffer(1, **resources.buffer(instanceHandle));
                      // The shared shader declares this slot for all modes; ordinary draws
                      // bind a zero dummy and never address it through the batched branch.
                      commands->bindBuffer(4, **resources.buffer(idsHandle));
                      commands->bindBuffer(5, **resources.buffer(colorsHandle));
                      if (batched) {
                          for (uint32_t bin = 0; bin < spec.bins; ++bin) {
                              if (binOffsets[bin] != binOffsets[bin + 1]) {
                                  commands->bindBuffer(0, **resources.buffer(paramHandles[bin]));
                                  commands->drawIndirect(**resources.buffer(drawArgs),
                                                         uint64_t{bin} *
                                                             sizeof(rhi::DrawIndirectArgs));
                              }
                          }
                      } else if (indirect) {
                          const size_t draws = gpuArgs ? spec.count : visibleIds.size();
                          for (size_t draw = 0; draw < draws; ++draw) {
                              commands->drawIndirect(**resources.buffer(drawArgs),
                                                     draw * sizeof(rhi::DrawIndirectArgs));
                          }
                      } else {
                          for (uint32_t id : visibleIds) {
                              commands->draw(vertices, id * vertices);
                          }
                      }
                  });

    render::CopyPassDesc readback;
    readback.textureSources = {render::nextVersion(colorHandle)};
    readback.bufferDestinations = {pixelsHandle};
    if (indirect) {
        readback.bufferSources = {argsHandle};
        readback.bufferDestinations.push_back(readArgsHandle);
    }
    if (batched) {
        readback.bufferSources.push_back(idsHandle);
        readback.bufferDestinations.push_back(readIdsHandle);
    }
    graph.addCopyPass("submission.reference.readback", std::move(readback),
                      [&, finalArgs = argsHandle](const render::PassResources& resources) {
                          commands->copyTextureToBuffer(
                              **resources.texture(render::nextVersion(colorHandle)),
                              {.width = kExtent, .height = kExtent},
                              **resources.buffer(pixelsHandle), {.bytesPerRow = kRowBytes});
                          if (indirect) {
                              auto& source = **resources.buffer(finalArgs);
                              commands->copyBuffer(source, 0, **resources.buffer(readArgsHandle), 0,
                                                   source.size());
                          }
                          if (batched) {
                              auto& source = **resources.buffer(idsHandle);
                              commands->copyBuffer(source, 0, **resources.buffer(readIdsHandle), 0,
                                                   source.size());
                          }
                      });
    graph.readbackBuffer(render::nextVersion(pixelsHandle));
    if (indirect) {
        graph.readbackBuffer(render::nextVersion(readArgsHandle));
    }
    if (batched) {
        graph.readbackBuffer(render::nextVersion(readIdsHandle));
    }
    auto compiled = graph.compile();
    if (!compiled) {
        return std::unexpected("Compile reference graph: " + compiled.error().message);
    }
    commands = &device->beginFrame();
    const auto record = graph.execute(*commands, device->frameNumber());
    device->endFrame(nullptr);
    // All imports, pipelines, and libraries remain owned by this call through exact retirement.
    device->waitIdle();

    FrameImage image;
    image.graphDump = render::dumpCompiledFrame(record);
    image.rgba.resize(kImageBytes);
    (*pixelReadback)->readback(image.rgba.data(), image.rgba.size());
    if (indirect && !arguments.empty()) {
        std::vector<rhi::DrawIndirectArgs> actual(arguments.size());
        (*argumentReadback)->readback(actual.data(), actual.size() * sizeof(actual.front()));
        if (gpuArgs) {
            if (!sameArgs(actual.back(), kUnwritten)) {
                return std::unexpected("Reference preparation overwrote its dispatch-tail guard");
            }
            for (uint32_t id = 0; id < spec.count; ++id) {
                const auto& arg = actual[id];
                if (arg.firstVertex != id * vertices || arg.firstInstance != 0 ||
                    arg.instanceCount > 1 || arg.vertexCount != vertices) {
                    return std::unexpected("Invalid generated reference argument at object " +
                                           std::to_string(id));
                }
                if (arg.instanceCount != 0) {
                    image.visibleIds.push_back(arg.firstVertex / vertices);
                }
            }
        } else {
            for (size_t index = 0; index < arguments.size(); ++index) {
                if (!sameArgs(actual[index], arguments[index])) {
                    return std::unexpected("Reference CPU arguments changed during execution");
                }
                if (!batched) {
                    image.visibleIds.push_back(actual[index].firstVertex / vertices);
                }
            }
        }
    }
    if (batched) {
        image.visibleIds.resize(visibleIds.size());
        if (!image.visibleIds.empty()) {
            idsReadback->readback(image.visibleIds.data(),
                                  image.visibleIds.size() * sizeof(uint32_t));
        }
        if (image.visibleIds != visibleIds) {
            return std::unexpected("Reference packed visible IDs changed during execution");
        }
    } else if (variant == Variant::Direct) {
        image.visibleIds = std::move(visibleIds);
    }
    return image;
}

} // namespace lmx::experimental::submission
