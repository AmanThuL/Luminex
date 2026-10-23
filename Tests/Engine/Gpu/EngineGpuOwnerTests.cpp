#include <catch2/catch_test_macros.hpp>

#include "Engine/Gpu/GpuBuffer.h"
#include "Engine/Gpu/GpuOwned.h"
#include "Engine/Gpu/GpuTexture.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace {

// Stands in for a device texture; the owner never calls through it, so the overrides are trivial.
class StubTexture final : public rojoRHI::Texture {
public:
    //==================================================================================================================
    explicit StubTexture(bool* destroyed = nullptr) : m_destroyed(destroyed) {}

    //==================================================================================================================
    ~StubTexture() override {
        if (m_destroyed != nullptr) {
            *m_destroyed = true;
        }
    }

    //==================================================================================================================
    uint32_t width() const override { return 0; }

    //==================================================================================================================
    uint32_t height() const override { return 0; }

    //==================================================================================================================
    rojoRHI::Format format() const override { return rojoRHI::Format::BGRA8Unorm; }

    //==================================================================================================================
    uint32_t mipLevels() const override { return 1; }

    //==================================================================================================================
    uint32_t arrayLayers() const override { return 1; }

    //==================================================================================================================
    void readback(void*, uint64_t) override {}

private:
    bool* m_destroyed;
};

// Stands in for a device buffer, for the same reason as StubTexture.
class StubBuffer final : public rojoRHI::Buffer {
public:
    //==================================================================================================================
    uint64_t size() const override { return 0; }

    //==================================================================================================================
    void readback(void*, uint64_t) override {}

    //==================================================================================================================
    void write(uint64_t, const void*, uint64_t) override {}
};

//======================================================================================================================
// Whether `text` lies inside the bytes of `owner`, which is where a short label's inline storage
// lives once the owner holds it.
template <typename Owner>
bool viewsStorageOf(std::string_view text, const Owner& owner) {
    const auto* begin = reinterpret_cast<const std::byte*>(&owner);
    const auto* data = reinterpret_cast<const std::byte*>(text.data());
    return data >= begin && data < begin + sizeof(Owner);
}

} // namespace

//======================================================================================================================
TEST_CASE("GpuTexture keeps a descriptor copy whose label outlives the caller's string",
          "[engine]") {
    lmx::engine::GpuTexture texture;
    {
        std::string label = "luminex.test.texture-with-a-label-longer-than-inline-storage";
        rojoRHI::TextureDesc desc{.width = 64, .height = 32, .sampled = true, .label = label};
        texture = lmx::engine::GpuTexture::adopt(std::make_unique<StubTexture>(), desc);
        label.assign(label.size(), 'x');
    }

    CHECK(texture.desc().label == "luminex.test.texture-with-a-label-longer-than-inline-storage");
    CHECK(texture.desc().width == 64);
    CHECK(texture.desc().height == 32);
}

//======================================================================================================================
TEST_CASE("GpuTexture moves without dangling its label", "[engine]") {
    const rojoRHI::TextureDesc desc{.width = 8, .height = 8, .label = "short"};
    lmx::engine::GpuTexture source =
        lmx::engine::GpuTexture::adopt(std::make_unique<StubTexture>(), desc);
    rojoRHI::Texture* const object = &source.rhi();

    lmx::engine::GpuTexture constructed{std::move(source)};

    CHECK(constructed.desc().label == "short");
    CHECK(viewsStorageOf(constructed.desc().label, constructed));
    CHECK(&constructed.rhi() == object);
    CHECK_FALSE(source); // NOLINT(bugprone-use-after-move,clang-analyzer-cplusplus.Move)

    lmx::engine::GpuTexture assigned;
    assigned = std::move(constructed);

    CHECK(assigned.desc().label == "short");
    CHECK(viewsStorageOf(assigned.desc().label, assigned));
    CHECK(&assigned.rhi() == object);
    CHECK_FALSE(constructed); // NOLINT(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
}

//======================================================================================================================
TEST_CASE("GpuBuffer keeps size and usage in its descriptor", "[engine]") {
    const rojoRHI::BufferDesc desc{
        .size = 4096, .storageRead = true, .cpuReadback = true, .label = "luminex.test.buffer"};

    const lmx::engine::GpuBuffer buffer =
        lmx::engine::GpuBuffer::adopt(std::make_unique<StubBuffer>(), desc);

    REQUIRE(buffer);
    CHECK(buffer.desc().size == 4096);
    CHECK(buffer.desc().storageRead);
    CHECK_FALSE(buffer.desc().storageWrite);
    CHECK(buffer.desc().cpuReadback);
    CHECK_FALSE(buffer.desc().cpuWrite);
    CHECK(buffer.desc().label == "luminex.test.buffer");
}

//======================================================================================================================
TEST_CASE("GpuOwned reset releases the object", "[engine]") {
    bool destroyed = false;
    lmx::engine::GpuOwned<rojoRHI::Texture> owned{std::make_unique<StubTexture>(&destroyed)};
    REQUIRE(owned);

    owned.reset();

    CHECK(destroyed);
    CHECK_FALSE(owned);
}
