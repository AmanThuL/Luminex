// Standalone native Metal 4 spike. Experimental source never joins the production branch.
#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#include <CoreFoundation/CoreFoundation.h>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <objc/runtime.h>
#include <stdexcept>
#include <thread>
#include <vector>

NS::String* str(const std::string& s) {
    return NS::String::string(s.c_str(), NS::UTF8StringEncoding);
}
void check(bool ok, const std::string& s, NS::Error* e = nullptr) {
    if (!ok)
        throw std::runtime_error(
            s + (e ? ": " + std::string(e->localizedDescription()->utf8String()) : ""));
}
template <class T>
T* label(T* p, const std::string& s) {
    check(p, "create " + s);
    p->setLabel(str(s));
    return p;
}
constexpr uint32_t kWidth = 128, kHeight = 96;
struct Slot {
    MTL4::CommandAllocator* allocator;
    MTL::IndirectCommandBuffer* icb;
    MTL::Buffer* native;
    MTL::Buffer* words;
    MTL::Buffer* frame;
    MTL4::ArgumentTable* compute;
    MTL4::ArgumentTable* array;
    std::array<MTL4::ArgumentTable*, 2> fixed;
    std::array<MTL::Texture*, 2> output;
    uint64_t last = 0;
};
int main(int argc, char** argv) {
    auto* pool = NS::AutoreleasePool::alloc()->init();
    try {
        check(argc >= 4, "usage: Runtime SHADER_DIR OUTPUT_DIR FRAMES [CAPTURE_PATH]");
        const std::filesystem::path shaders = argv[1], output = argv[2];
        std::filesystem::create_directories(output);
        const uint32_t frames = std::stoul(argv[3]);
        const bool fixedControl = std::getenv("LMX_SPIKE_FIXED_ONLY") != nullptr;
        std::cout << "fixedControl=" << fixedControl << std::endl;
        check(frames > 0, "frames positive");
        auto* device = MTL::CreateSystemDefaultDevice();
        check(device, "device");
        std::cout << "device=" << device->name()->utf8String()
                  << " metal4=" << device->supportsFamily(MTL::GPUFamilyMetal4) << std::endl;
        check(device->supportsFamily(MTL::GPUFamilyMetal4), "Metal4 required");
        std::cout << "deviceClass=" << object_getClassName((id)device) << std::endl;
        NS::Error* error = nullptr;
        auto* qdesc = MTL4::CommandQueueDescriptor::alloc()->init();
        qdesc->setLabel(str("lmx.spike.icb.queue"));
        auto* queue = device->newMTL4CommandQueue(qdesc, &error);
        check(queue, "queue", error);
        auto* compiler = device->newCompiler(MTL4::CompilerDescriptor::alloc()->init(), &error);
        check(compiler, "compiler", error);
        auto* residency =
            device->newResidencySet(MTL::ResidencySetDescriptor::alloc()->init(), &error);
        check(residency, "residency", error);
        queue->addResidencySet(residency);
        auto add = [&](auto* resource) {
            check(resource, "resource creation");
            residency->addAllocation(resource);
            return resource;
        };
        auto buffer = [&](size_t n, const void* bytes, const std::string& s) {
            auto* b = label(add(device->newBuffer(n, MTL::ResourceStorageModeShared)), s);
            if (bytes)
                std::memcpy(b->contents(), bytes, n);
            else
                std::memset(b->contents(), 0, n);
            return b;
        };
        auto library = [&](const char* stem) {
            auto path = (shaders / (std::string(stem) + ".metallib")).string();
            auto* lib = device->newLibrary(NS::URL::fileURLWithPath(str(path)), &error);
            check(lib, path, error);
            return label(lib, "lmx.spike." + std::string(stem));
        };
        auto* gen = library("Generate");
        auto* arrayLib = library("ArrayDraw");
        auto* fixedLib = library("FixedDraw");
        auto function = [&](MTL::Library* lib, const char* name) {
            auto* f = MTL4::LibraryFunctionDescriptor::alloc()->init();
            f->setLibrary(lib);
            f->setName(str(name));
            return f;
        };
        auto compute = [&](const char* name) {
            auto* d = MTL4::ComputePipelineDescriptor::alloc()->init();
            d->setComputeFunctionDescriptor(function(gen, name));
            d->setLabel(str("lmx.spike." + std::string(name)));
            auto* p = compiler->newComputePipelineState(d, nullptr, &error);
            check(p, name, error);
            return p;
        };
        auto* classify = compute("classify");
        auto* emit = compute("emitCommands");
        auto graphics = [&](MTL::Library* lib, bool icb) {
            auto* d = MTL4::RenderPipelineDescriptor::alloc()->init();
            d->setVertexFunctionDescriptor(function(lib, "vertexMain"));
            d->setFragmentFunctionDescriptor(function(lib, "fragmentMain"));
            d->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
            d->setRasterSampleCount(1);
            d->setSupportIndirectCommandBuffers(
                icb ? MTL4::IndirectCommandBufferSupportStateEnabled
                    : MTL4::IndirectCommandBufferSupportStateDisabled);
            d->setLabel(str(icb ? "lmx.spike.array.pipeline" : "lmx.spike.fixed.pipeline"));
            auto* p = compiler->newRenderPipelineState(d, nullptr, &error);
            check(p, "graphics pipeline", error);
            return p;
        };
        auto* arrayPSO = graphics(arrayLib, true);
        auto* fixedPSO = graphics(fixedLib, false);
        const float positions[6][4] = {{-.9f, -.8f, 0, 1}, {-.1f, -.8f, 0, 1}, {-.5f, .8f, 0, 1},
                                       {.1f, -.7f, 0, 1},  {.9f, -.7f, 0, 1},  {.7f, .65f, 0, 1}};
        const uint32_t indices[6] = {0, 1, 2, 3, 4, 5};
        auto* vertices = buffer(sizeof(positions), positions, "lmx.spike.vertices");
        auto* indexBuffer = buffer(sizeof(indices), indices, "lmx.spike.indices");
        auto* sampDesc = MTL::SamplerDescriptor::alloc()->init();
        sampDesc->setSupportArgumentBuffers(true);
        sampDesc->setLabel(str("lmx.spike.sampler"));
        auto* sampler = device->newSamplerState(sampDesc);
        check(sampler, "sampler");
        std::array<MTL::Texture*, 10> textures;
        for (uint32_t i = 0; i < 10; ++i) {
            auto* d = MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, 1, 1,
                                                                  false);
            d->setStorageMode(MTL::StorageModeShared);
            d->setUsage(MTL::TextureUsageShaderRead);
            textures[i] =
                label(add(device->newTexture(d)), "lmx.spike.texture." + std::to_string(i));
            uint8_t color[4] = {uint8_t(15 + i * 21), uint8_t(210 - i * 17), uint8_t(40 + i * 13),
                                255};
            textures[i]->replaceRegion(MTL::Region::Make2D(0, 0, 1, 1), 0, color, 4);
        }
        auto* frag = arrayLib->newFunction(str("fragmentMain"));
        check(frag, "fragment function");
        auto* textureEncoder = frag->newArgumentEncoder(1);
        check(textureEncoder, "texture argument encoder");
        auto* textureArgs =
            buffer(textureEncoder->encodedLength(), nullptr, "lmx.spike.textureArguments");
        textureEncoder->setArgumentBuffer(textureArgs, 0);
        for (uint32_t i = 0; i < 10; ++i)
            textureEncoder->setTexture(textures[i], i);
        textureEncoder->setSamplerState(sampler, 10);
        std::cout << "textureArgumentBytes=" << textureEncoder->encodedLength() << std::endl;
        auto* commandArg = MTL::ArgumentDescriptor::alloc()->init();
        commandArg->setDataType(MTL::DataTypeIndirectCommandBuffer);
        commandArg->setIndex(0);
        commandArg->setAccess(MTL::ArgumentAccessReadWrite);
        auto* indexArg = MTL::ArgumentDescriptor::alloc()->init();
        indexArg->setDataType(MTL::DataTypePointer);
        indexArg->setIndex(1);
        const NS::Object* descriptors[] = {commandArg, indexArg};
        auto* nativeEncoder = device->newArgumentEncoder(NS::Array::array(descriptors, 2));
        check(nativeEncoder, "native argument encoder");
        auto table = [&](const std::string& s) {
            auto* d = MTL4::ArgumentTableDescriptor::alloc()->init();
            d->setInitializeBindings(true);
            d->setMaxBufferBindCount(3);
            d->setMaxTextureBindCount(5);
            d->setMaxSamplerStateBindCount(1);
            d->setLabel(str(s));
            auto* t = device->newArgumentTable(d, &error);
            check(t, "argument table", error);
            return t;
        };
        std::array<Slot, 3> slots;
        for (uint32_t i = 0; i < 3; ++i) {
            auto& s = slots[i];
            std::string suffix = std::to_string(i);
            auto* ad = MTL4::CommandAllocatorDescriptor::alloc()->init();
            ad->setLabel(str("lmx.spike.allocator." + suffix));
            s.allocator = device->newCommandAllocator(ad, &error);
            check(s.allocator, "allocator", error);
            auto* id = MTL::IndirectCommandBufferDescriptor::alloc()->init();
            id->setCommandTypes(MTL::IndirectCommandTypeDrawIndexed);
            id->setInheritBuffers(true);
            id->setInheritPipelineState(true);
            s.icb =
                label(add(device->newIndirectCommandBuffer(id, 2, MTL::ResourceStorageModePrivate)),
                      "lmx.spike.commands." + suffix);
            s.native =
                buffer(nativeEncoder->encodedLength(), nullptr, "lmx.spike.native." + suffix);
            nativeEncoder->setArgumentBuffer(s.native, 0);
            nativeEncoder->setIndirectCommandBuffer(s.icb, 0);
            nativeEncoder->setBuffer(indexBuffer, 0, 1);
            s.words = buffer(40, nullptr, "lmx.spike.visibility." + suffix);
            s.frame = buffer(16, nullptr, "lmx.spike.frame." + suffix);
            s.compute = table("lmx.spike.computeArgs." + suffix);
            s.compute->setAddress(s.native->gpuAddress(), 0);
            s.compute->setAddress(s.words->gpuAddress(), 1);
            s.compute->setAddress(s.frame->gpuAddress(), 2);
            s.array = table("lmx.spike.arrayArgs." + suffix);
            s.array->setAddress(vertices->gpuAddress(), 0);
            s.array->setAddress(textureArgs->gpuAddress(), 1);
            for (uint32_t mat = 0; mat < 2; ++mat) {
                s.fixed[mat] = table("lmx.spike.fixedArgs." + suffix + "." + std::to_string(mat));
                s.fixed[mat]->setAddress(vertices->gpuAddress(), 0);
                s.fixed[mat]->setSamplerState(sampler->gpuResourceID(), 0);
                for (uint32_t j = 0; j < 5; ++j)
                    s.fixed[mat]->setTexture(textures[mat * 5 + j]->gpuResourceID(), j);
                auto* d = MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm,
                                                                      kWidth, kHeight, false);
                d->setStorageMode(MTL::StorageModeShared);
                d->setUsage(MTL::TextureUsageRenderTarget);
                s.output[mat] = label(add(device->newTexture(d)),
                                      "lmx.spike.output." + suffix + "." + std::to_string(mat));
            }
        }
        residency->commit();
        auto* event = label(device->newSharedEvent(), "lmx.spike.retirement");
        event->setSignaledValue(0);
        std::atomic<uint32_t> feedbackErrors = 0, feedbackCount = 0;
        auto* feedbackSemaphore = dispatch_semaphore_create(0);
        CA::MetalLayer* layer = nullptr;
        if (std::getenv("LMX_SPIKE_PRESENT")) {
            layer = CA::MetalLayer::layer();
            layer->setDevice(device);
            layer->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
            layer->setDrawableSize({128, 96});
        }
        bool captured = false;
        MTL::CaptureScope* scope = nullptr;
        auto beginCapture = [&]() {
            if (std::getenv("LMX_SPIKE_SCOPE_CAPTURE")) {
                scope = MTL::CaptureManager::sharedCaptureManager()->newCaptureScope(queue);
                check(scope, "capture scope");
                scope->setLabel(str("lmx.spike.indexed.capture"));
            }
            auto* d = MTL::CaptureDescriptor::alloc()->init();
            d->setCaptureObject(scope ? static_cast<NS::Object*>(scope)
                                      : static_cast<NS::Object*>(device));
            d->setDestination(MTL::CaptureDestinationGPUTraceDocument);
            d->setOutputURL(NS::URL::alloc()->initFileURLWithPath(str(argv[4])));
            check(MTL::CaptureManager::sharedCaptureManager()->startCapture(d, &error),
                  "start capture", error);
            captured = true;
            if (scope)
                scope->beginScope();
            std::cout << "captureStarted="
                      << MTL::CaptureManager::sharedCaptureManager()->isCapturing() << std::endl;
        };
        auto* cb = label(device->newCommandBuffer(), "lmx.spike.frame");
        uint32_t verified = 0;
        auto verify = [&](Slot& s) {
            if (!s.last)
                return;
            check(event->waitUntilSignaledValue(s.last, 10000),
                  "retirement timeout frame " + std::to_string(s.last));
            std::array<std::vector<uint8_t>, 2> pixels = {
                std::vector<uint8_t>(kWidth * kHeight * 4),
                std::vector<uint8_t>(kWidth * kHeight * 4)};
            for (uint32_t i = 0; i < 2; ++i)
                s.output[i]->getBytes(pixels[i].data(), kWidth * 4,
                                      MTL::Region::Make2D(0, 0, kWidth, kHeight), 0);
            size_t delta = 0, nonzero = 0;
            for (size_t i = 0; i < pixels[0].size(); ++i) {
                delta += pixels[0][i] != pixels[1][i];
                if (i % 4 < 3)
                    nonzero += pixels[0][i] != 0;
            }
            if (s.last == 1 || delta) {
                for (uint32_t i = 0; i < 2; ++i) {
                    std::ofstream file(output / (std::string(i ? "fixed-" : "icb-") +
                                                 std::to_string(s.last) + ".rgba"),
                                       std::ios::binary);
                    file.write(reinterpret_cast<char*>(pixels[i].data()), pixels[i].size());
                }
            }
            const std::array<uint8_t, 4> left = {57, 176, 66, 255}, right = {162, 91, 131, 255};
            uint32_t leftPixels = 0, rightPixels = 0;
            for (size_t i = 0; i < pixels[0].size(); i += 4) {
                leftPixels += std::memcmp(pixels[0].data() + i, left.data(), 4) == 0;
                rightPixels += std::memcmp(pixels[0].data() + i, right.data(), 4) == 0;
            }
            check(leftPixels == 1950 && rightPixels == (s.last % 4 == 3 ? 0u : 1680u),
                  "expected five-texture material colors/coverage frame " + std::to_string(s.last));
            check(nonzero > 100, "blank output frame " + std::to_string(s.last));
            check(delta == 0, "image mismatch bytes=" + std::to_string(delta) +
                                  " frame=" + std::to_string(s.last));
            const uint32_t* words = static_cast<const uint32_t*>(s.words->contents());
            check(words[0] == 3 && words[2] == 0 && words[5] == 3 && words[7] == 3 && words[9] == 1,
                  "GPU argument readback");
            ++verified;
            s.last = 0;
        };
        for (uint32_t frame = 1; frame <= frames; ++frame) {
            auto* fp = NS::AutoreleasePool::alloc()->init();
            auto& s = slots[frame % 3];
            verify(s);
            s.allocator->reset();
            *static_cast<uint32_t*>(s.frame->contents()) = frame;
            if (argc > 4 && frame == std::min(3u, frames)) {
                check(event->waitUntilSignaledValue(frame - 1, 10000), "capture warmup retirement");
                beginCapture();
            }
            CA::MetalDrawable* drawable = layer ? layer->nextDrawable() : nullptr;
            check(!layer || drawable, "presentation drawable");
            cb->beginCommandBuffer(s.allocator);
            auto* reset = cb->computeCommandEncoder();
            reset->setLabel(str("lmx.pass.spike.icb.reset"));
            if (!fixedControl)
                reset->resetCommandsInBuffer(s.icb, NS::Range::Make(0, 2));
            reset->endEncoding();
            auto* c = cb->computeCommandEncoder();
            c->setLabel(str("lmx.pass.spike.visibility.classify"));
            c->setArgumentTable(s.compute);
            c->setComputePipelineState(classify);
            c->dispatchThreadgroups(MTL::Size::Make(2, 1, 1), MTL::Size::Make(1, 1, 1));
            c->endEncoding();
            c = cb->computeCommandEncoder();
            c->setLabel(str("lmx.pass.spike.visibility.emitCommands"));
            c->barrierAfterQueueStages(MTL::StageAll, MTL::StageDispatch,
                                       MTL4::VisibilityOptionDevice);
            c->setArgumentTable(s.compute);
            c->setComputePipelineState(emit);
            if (!fixedControl)
                c->dispatchThreadgroups(MTL::Size::Make(2, 1, 1), MTL::Size::Make(1, 1, 1));
            c->endEncoding();
            for (uint32_t path = 0; path < 2; ++path) {
                auto* d = MTL4::RenderPassDescriptor::alloc()->init();
                auto* a = d->colorAttachments()->object(0);
                a->setTexture(s.output[path]);
                a->setLoadAction(MTL::LoadActionClear);
                a->setStoreAction(MTL::StoreActionStore);
                a->setClearColor(MTL::ClearColor::Make(0, 0, 0, 1));
                auto* r = cb->renderCommandEncoder(d);
                r->setLabel(str(path ? "lmx.pass.spike.fixed.draw" : "lmx.pass.spike.icb.execute"));
                r->barrierAfterQueueStages(MTL::StageAll, MTL::StageVertex | MTL::StageFragment,
                                           MTL4::VisibilityOptionDevice);
                r->setViewport({0, 0, kWidth, kHeight, 0, 1});
                r->setCullMode(MTL::CullModeNone);
                r->setRenderPipelineState(path ? fixedPSO : arrayPSO);
                if (path == 0) {
                    r->setArgumentTable(s.array, MTL::RenderStageVertex | MTL::RenderStageFragment);
                    if (!fixedControl)
                        r->executeCommandsInBuffer(s.icb, NS::Range::Make(0, 2));
                    else
                        for (uint32_t m = 0; m < 2; ++m)
                            r->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle,
                                                     MTL::IndexTypeUInt32,
                                                     indexBuffer->gpuAddress(), sizeof(indices),
                                                     s.words->gpuAddress() + m * 20);
                } else
                    for (uint32_t m = 0; m < 2; ++m) {
                        r->setArgumentTable(s.fixed[m],
                                            MTL::RenderStageVertex | MTL::RenderStageFragment);
                        r->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, MTL::IndexTypeUInt32,
                                                 indexBuffer->gpuAddress(), sizeof(indices),
                                                 s.words->gpuAddress() + m * 20);
                    }
                r->endEncoding();
                d->release();
            }
            if (drawable) {
                auto* d = MTL4::RenderPassDescriptor::alloc()->init();
                auto* a = d->colorAttachments()->object(0);
                a->setTexture(drawable->texture());
                a->setLoadAction(MTL::LoadActionClear);
                a->setStoreAction(MTL::StoreActionStore);
                a->setClearColor(MTL::ClearColor::Make(0, 0, 0, 1));
                auto* r = cb->renderCommandEncoder(d);
                r->setLabel(str("lmx.pass.spike.presentation"));
                r->endEncoding();
                d->release();
                queue->wait(drawable);
            }
            cb->endCommandBuffer();
            const MTL4::CommandBuffer* buffers[] = {cb};
            auto* options = MTL4::CommitOptions::alloc()->init();
            options->addFeedbackHandler(
                MTL4::CommitFeedbackHandlerFunction([&](MTL4::CommitFeedback* feedback) {
                    if (feedback->error()) {
                        ++feedbackErrors;
                        std::cerr << "GPU_ERROR "
                                  << feedback->error()->localizedDescription()->utf8String()
                                  << std::endl;
                    }
                    ++feedbackCount;
                    dispatch_semaphore_signal(feedbackSemaphore);
                }));
            queue->commit(buffers, 1, options);
            options->release();
            if (drawable) {
                queue->signalDrawable(drawable);
                drawable->present();
            }
            queue->signalEvent(event, frame);
            s.last = frame;
            fp->release();
        }
        for (auto& s : slots)
            verify(s);
        for (uint32_t i = 0; i < frames; ++i)
            check(dispatch_semaphore_wait(feedbackSemaphore,
                                          dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC)) == 0,
                  "GPU feedback timeout");
        if (captured) {
            if (scope)
                scope->endScope();
            MTL::CaptureManager::sharedCaptureManager()->stopCapture();
            for (uint32_t i = 0; i < 40 && !std::filesystem::exists(argv[4]); ++i)
                CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
            check(std::filesystem::exists(argv[4]), "capture bundle missing after stopCapture");
        }
        check(feedbackErrors == 0, "GPU feedback errors");
        std::cout << "PASS verifiedFrames=" << verified
                  << " slots=3 feedbackCount=" << feedbackCount
                  << " feedbackErrors=" << feedbackErrors
                  << " indexed=1 texturesPerMaterial=5 materials=2 meshes=2 exactBytes=1"
                  << std::endl;
        pool->release();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << e.what() << std::endl;
        pool->release();
        return 1;
    }
}
