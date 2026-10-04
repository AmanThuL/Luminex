#include "Engine/Asset/Document/SceneDocument.h"

#include "Core/IO/File.h"
#include "Core/Util/Sha256.h"
#include "Engine/Asset/Document/SceneDocumentSaveInternal.h"
#include "Engine/Asset/Image/PngImage.h"
#include "Scenes/SceneDocumentExport.h"
#include "Support/SceneDocumentFixtures.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>

using namespace lmx::asset;
namespace fs = std::filesystem;

namespace {

//======================================================================================================================
void replace(std::string& text, std::string_view before, std::string_view after) {
    const auto offset = text.find(before);
    REQUIRE(offset != std::string::npos);
    text.replace(offset, before.size(), after);
}

//======================================================================================================================
void writeBytes(const fs::path& path, std::span<const std::byte> bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    REQUIRE(file.good());
    file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    REQUIRE(file.good());
}

struct Fixture {
    fs::path directory = "SceneDocuments/content-reader";
    fs::path path = directory / "cube.scene.gltf";
    std::string json;
    std::vector<std::byte> geometry;
    std::array<uint8_t, 64> color{};
    std::array<uint8_t, 8> linear{128, 128, 255, 255, 0, 127, 255, 255};

    //==================================================================================================================
    Fixture() {
        fs::remove_all(directory);
        fs::create_directories(directory / "cube.scene.textures");
        // Eight authored cube corners: deliberately non-unit normal/tangent bits survive reading.
        for (uint32_t v = 0; v < 8; ++v) {
            const std::array<float, 12> vertex{v & 1 ? 1.f : -1.f,
                                               v & 2 ? 1.f : -1.f,
                                               v & 4 ? 1.f : -1.f,
                                               0.f,
                                               0.f,
                                               0.99999f,
                                               0.99998f,
                                               -0.f,
                                               0.f,
                                               -1.f,
                                               float(v & 1),
                                               float(v & 2)};
            for (float value : vertex) {
                const auto bits = std::bit_cast<uint32_t>(value);
                for (unsigned shift = 0; shift < 32; shift += 8)
                    geometry.push_back(std::byte(bits >> shift));
            }
        }
        for (uint32_t index :
             {0u, 2u, 1u, 1u, 2u, 3u, 4u, 5u, 6u, 5u, 7u, 6u, 0u, 1u, 4u, 1u, 5u, 4u,
              2u, 6u, 3u, 3u, 6u, 7u, 0u, 4u, 2u, 2u, 4u, 6u, 1u, 3u, 5u, 3u, 7u, 5u})
            for (unsigned shift = 0; shift < 32; shift += 8)
                geometry.push_back(std::byte(index >> shift));
        for (size_t i = 0; i < color.size(); ++i)
            color[i] = i % 4 == 3 ? 255 : uint8_t(i * 3);
        const auto model = lmx::test::contentDocument();
        writeBytes(directory / "cube.scene.textures/color.png", model.content->images[0].file);
        writeBytes(directory / "cube.scene.textures/linear.png", model.content->images[1].file);
        writeBytes(directory / "cube.scene.geometry.bin", geometry);
        json = R"({
"asset":{"version":"2.0"},"extensionsUsed":["LMX_scene","KHR_materials_emissive_strength"],
"scene":0,"scenes":[{"name":"Content test","nodes":[0,1,2]}],
"nodes":[{"name":"Camera","camera":0,"extensions":{"LMX_scene":{"enabled":true}}},
{"name":"First cube","mesh":0,"translation":[1.25,2,3],"extensions":{"LMX_scene":{"enabled":true}}},
{"name":"Second cube","mesh":1,"scale":[2,3,4],"extensions":{"LMX_scene":{"enabled":false,"motion":"invalid"}}}],
"cameras":[{"name":"Perspective","type":"perspective","perspective":{"yfov":1.0471976,"znear":0.1,"zfar":100}}],
"meshes":[{"name":"Cube color","primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TANGENT":2,"TEXCOORD_0":3},"indices":4,"material":0}]},
{"name":"Cube data","primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TANGENT":2,"TEXCOORD_0":3},"indices":4,"material":1}]}],
"materials":[{"name":"Color","pbrMetallicRoughness":{"baseColorFactor":[0.2,0.3,0.4,0.5],"metallicFactor":0.25,"roughnessFactor":0.75,"baseColorTexture":{"index":0}},"emissiveFactor":[0.1,0.2,0.3],"emissiveTexture":{"index":0},"extensions":{"KHR_materials_emissive_strength":{"emissiveStrength":2.5}}},
{"name":"Data","pbrMetallicRoughness":{"metallicRoughnessTexture":{"index":1}},"normalTexture":{"index":1},"occlusionTexture":{"index":1,"strength":0.625},"alphaMode":"MASK","alphaCutoff":0.375,"doubleSided":true}],
"textures":[{"source":0,"sampler":0},{"source":1,"sampler":1}],
"samplers":[{"minFilter":9987},{"minFilter":9729}],
"images":[{"name":"color","uri":"cube.scene.textures/color.png"},{"name":"linear","uri":"cube.scene.textures/linear.png"}],
"buffers":[{"uri":"cube.scene.geometry.bin","byteLength":528}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":384,"byteStride":48,"target":34962},{"buffer":0,"byteOffset":384,"byteLength":144,"target":34963}],
"accessors":[{"bufferView":0,"byteOffset":0,"componentType":5126,"count":8,"type":"VEC3","min":[-1,-1,-1],"max":[1,1,1]},
{"bufferView":0,"byteOffset":12,"componentType":5126,"count":8,"type":"VEC3"},
{"bufferView":0,"byteOffset":24,"componentType":5126,"count":8,"type":"VEC4"},
{"bufferView":0,"byteOffset":40,"componentType":5126,"count":8,"type":"VEC2"},
{"bufferView":1,"componentType":5125,"count":36,"type":"SCALAR"}],
"extensions":{"LMX_scene":{"schemaVersion":2,"camera":0,"loop":true,"bounds":{"min":[-2,-3,-4],"max":[5,6,7]},
"contentHashes":{"cube.scene.geometry.bin":"GEOMETRY","cube.scene.textures/color.png":"COLOR","cube.scene.textures/linear.png":"LINEAR"},
"look":{"exposure":{"ev":0,"autoEnabled":false,"lowPercentile":50,"highPercentile":95,"targetGrey":0.18,"evMin":-8,"evMax":8,"compensationEv":0,"adaptUpStopsPerSecond":3,"adaptDownStopsPerSecond":1.5},"bloom":{"enabled":true,"threshold":1,"intensity":0.2},"shadow":{"filter":"pcf"},"environment":{"skySrgb8":[149,170,196]}}}}})";
        replace(json, "GEOMETRY", lmx::sha256Hex(geometry));
        replace(json, "COLOR",
                lmx::sha256Hex(*lmx::readWholeFile(directory / "cube.scene.textures/color.png")));
        replace(json, "LINEAR",
                lmx::sha256Hex(*lmx::readWholeFile(directory / "cube.scene.textures/linear.png")));
    }

    //==================================================================================================================
    // Cases leave deliberately broken documents here, and Catch2 runs them in random order, so the
    // directory goes with the case rather than reaching the glTF validator in whatever state it
    // ended.
    ~Fixture() {
        std::error_code ignored;
        fs::remove_all(directory, ignored);
    }

    //==================================================================================================================
    AssetResult<SceneDocument> read() {
        writeBytes(path, std::as_bytes(std::span(json)));
        return readSceneDocument(path);
    }
};

//======================================================================================================================
void failsAt(Fixture& fixture, std::string_view pointer) {
    const auto result = fixture.read();
    REQUIRE_FALSE(result);
    INFO(result.error().message);
    CHECK(result.error().message.contains("JSON pointer '" + std::string(pointer) + "'"));
}

} // namespace

//======================================================================================================================
TEST_CASE("hand-authored schema 2 content reads without the document writer",
          "[asset][scene-document-content]") {
    Fixture fixture;
    const auto result = fixture.read();
    INFO((result ? "read succeeded" : result.error().message));
    REQUIRE(result);
    const auto& doc = *result;
    CHECK(kSceneDocumentSchema == 2);
    CHECK(doc.schemaVersion == 2);
    REQUIRE(doc.content);
    REQUIRE(doc.content->geometries.size() == 1);
    const auto& geometry = doc.content->geometries[0];
    REQUIRE(geometry.vertices.size() == 8);
    REQUIRE(geometry.indices.size() == 36);
    CHECK(std::memcmp(geometry.vertices.data(), fixture.geometry.data(), 384) == 0);
    CHECK(std::memcmp(geometry.indices.data(), fixture.geometry.data() + 384, 144) == 0);
    CHECK(doc.content->geometrySha256 == lmx::sha256Hex(fixture.geometry));
    REQUIRE(doc.meshes.size() == 2);
    CHECK(doc.meshes[0].geometry == 0);
    CHECK(doc.meshes[1].geometry == 0);
    CHECK(doc.meshes[0].material == 0);
    CHECK(doc.meshes[1].material == 1);
    CHECK(doc.nodes[1].mesh == 0);
    CHECK(doc.nodes[2].mesh == 1);
    CHECK(doc.nodes[1].motion == DocMotion::Rigid);
    CHECK(doc.nodes[2].motion == DocMotion::Invalid);
    CHECK(doc.nodes[1].translation == glm::vec3(1.25f, 2.f, 3.f));
    CHECK(doc.nodes[2].scale == glm::vec3(2.f, 3.f, 4.f));
    CHECK_FALSE(doc.nodes[2].enabled);
    REQUIRE(doc.bounds);
    CHECK(doc.bounds->first == glm::vec3(-2.f, -3.f, -4.f));
    CHECK(doc.bounds->second == glm::vec3(5.f, 6.f, 7.f));
    REQUIRE(doc.materials.size() == 2);
    const auto& color = doc.materials[0];
    CHECK(color.name == "Color");
    CHECK(color.emissiveStrength == 2.5f);
    CHECK(color.values.baseColorFactor == glm::vec4(0.2f, 0.3f, 0.4f, 0.5f));
    CHECK(color.values.baseColorImage == 0);
    CHECK(color.values.emissiveImage == 0);
    CHECK(color.values.emissiveFactor == glm::vec3(0.1f, 0.2f, 0.3f));
    CHECK(color.values.metallic == 0.25f);
    CHECK(color.values.roughness == 0.75f);
    const auto& data = doc.materials[1];
    CHECK(data.emissiveStrength == 1.f);
    CHECK(data.values.normalImage == 1);
    CHECK(data.values.metallicRoughnessImage == 1);
    CHECK(data.values.occlusionImage == 1);
    CHECK(data.values.occlusionStrength == 0.625f);
    CHECK(data.values.alphaMode == GltfAlphaMode::Mask);
    CHECK(data.values.alphaCutoff == 0.375f);
    CHECK(data.values.doubleSided);
    REQUIRE(doc.content->images.size() == 2);
    CHECK(doc.content->images[0].width == 4);
    CHECK(doc.content->images[0].height == 4);
    CHECK(doc.content->images[0].mipmapped);
    CHECK_FALSE(doc.content->images[1].mipmapped);
    REQUIRE(doc.content->images[0].rgba8.size() == fixture.color.size());
    CHECK(std::memcmp(doc.content->images[0].rgba8.data(), fixture.color.data(),
                      fixture.color.size()) == 0);
    for (const auto& image : doc.content->images) {
        CHECK(image.sha256 == lmx::sha256Hex(image.file));
        CHECK(image.file == *lmx::readWholeFile(fixture.directory / "cube.scene.textures" /
                                                (image.name + ".png")));
    }
    const SceneDocument copy = doc;
    CHECK(copy.content.get() == doc.content.get());
    CHECK_FALSE(doc.sourceBufferUri);
}

//======================================================================================================================
TEST_CASE("document content rejects malformed geometry at the precise field",
          "[asset][scene-document-content]") {
    Fixture fixture;
    std::string pointer;
    SECTION("missing attribute") {
        replace(fixture.json, ",\"TANGENT\":2", "");
        pointer = "/meshes/0/primitives/0/attributes";
    }
    SECTION("short indices") {
        replace(fixture.json, "\"componentType\":5125", "\"componentType\":5123");
        pointer = "/accessors/4/componentType";
    }
    SECTION("stride") {
        replace(fixture.json, "\"byteStride\":48", "\"byteStride\":32");
        pointer = "/bufferViews/0/byteStride";
    }
    SECTION("attribute view") {
        replace(fixture.json, "\"bufferView\":0,\"byteOffset\":12",
                "\"bufferView\":1,\"byteOffset\":12");
        pointer = "/accessors/1/bufferView";
    }
    SECTION("attribute offset") {
        replace(fixture.json, "\"byteOffset\":12", "\"byteOffset\":16");
        pointer = "/accessors/1/byteOffset";
    }
    SECTION("attribute count") {
        replace(fixture.json, "\"count\":8,\"type\":\"VEC4\"", "\"count\":7,\"type\":\"VEC4\"");
        pointer = "/accessors/2/count";
    }
    SECTION("sparse") {
        replace(fixture.json, "\"byteOffset\":24", "\"sparse\":{},\"byteOffset\":24");
        pointer = "/accessors/2/sparse";
    }
    SECTION("normalized") {
        replace(fixture.json, "\"byteOffset\":24", "\"normalized\":true,\"byteOffset\":24");
        pointer = "/accessors/2/normalized";
    }
    SECTION("wrong mode") {
        replace(fixture.json, "\"indices\":4", "\"mode\":5,\"indices\":4");
        pointer = "/meshes/0/primitives/0/mode";
    }
    SECTION("missing geometry") {
        replace(fixture.json,
                "\"buffers\":[{\"uri\":\"cube.scene.geometry.bin\",\"byteLength\":528}],", "");
        pointer = "/buffers";
    }
    SECTION("unknown buffer") {
        replace(fixture.json, "\"uri\":\"cube.scene.geometry.bin\"", "\"uri\":\"other.bin\"");
        pointer = "/buffers/0/uri";
    }
    SECTION("mesh index") {
        replace(fixture.json, "\"mesh\":0", "\"mesh\":2");
        pointer = "/nodes/1/mesh";
    }
    SECTION("material index") {
        replace(fixture.json, "\"material\":0", "\"material\":2");
        pointer = "/meshes/0/primitives/0/material";
    }
    SECTION("bounds") {
        replace(fixture.json, "\"min\":[-2,-3,-4]", "\"min\":[8,-3,-4]");
        pointer = "/extensions/LMX_scene/bounds/min";
    }
    SECTION("motion") {
        replace(fixture.json, "\"motion\":\"invalid\"", "\"motion\":\"moving\"");
        pointer = "/nodes/2/extensions/LMX_scene/motion";
    }
    SECTION("schema 1") {
        replace(fixture.json, "\"schemaVersion\":2", "\"schemaVersion\":1");
        pointer = "/meshes";
    }
    SECTION("schema 3") {
        replace(fixture.json, "\"schemaVersion\":2", "\"schemaVersion\":3");
        pointer = "/extensions/LMX_scene/schemaVersion";
    }
    failsAt(fixture, pointer);
}

//======================================================================================================================
TEST_CASE("document content file failures identify the URI reference",
          "[asset][scene-document-content]") {
    Fixture fixture;
    std::string pointer = "/buffers/0/uri";
    SECTION("missing geometry hash") {
        replace(fixture.json, "\"cube.scene.geometry.bin\":", "\"wrong.bin\":");
    }
    SECTION("modified geometry") {
        fixture.geometry[0] ^= std::byte{1};
        writeBytes(fixture.directory / "cube.scene.geometry.bin", fixture.geometry);
    }
    SECTION("truncated geometry") {
        fixture.geometry.pop_back();
        writeBytes(fixture.directory / "cube.scene.geometry.bin", fixture.geometry);
    }
    SECTION("missing geometry file") {
        fs::remove(fixture.directory / "cube.scene.geometry.bin");
    }
    SECTION("missing image hash") {
        replace(fixture.json, "\"cube.scene.textures/color.png\":", "\"missing.png\":");
        pointer = "/images/0/uri";
    }
    SECTION("missing image") {
        fs::remove(fixture.directory / "cube.scene.textures/color.png");
        pointer = "/images/0/uri";
    }
    SECTION("modified image") {
        writeBytes(fixture.directory / "cube.scene.textures/color.png", fixture.geometry);
        pointer = "/images/0/uri";
    }
    SECTION("bad PNG with matching hash") {
        const auto old = lmx::sha256Hex(
            *lmx::readWholeFile(fixture.directory / "cube.scene.textures/color.png"));
        replace(fixture.json, old, lmx::sha256Hex(fixture.geometry));
        writeBytes(fixture.directory / "cube.scene.textures/color.png", fixture.geometry);
        pointer = "/images/0/uri";
    }
    SECTION("data URI") {
        replace(fixture.json, "\"uri\":\"cube.scene.textures/color.png\"",
                "\"uri\":\"data:image/png;base64,AA==\"");
        pointer = "/images/0/uri";
    }
    SECTION("foreign folder") {
        replace(fixture.json, "\"uri\":\"cube.scene.textures/color.png\"",
                "\"uri\":\"other/color.png\"");
        pointer = "/images/0/uri";
    }
    SECTION("non PNG") {
        replace(fixture.json, "\"uri\":\"cube.scene.textures/color.png\"",
                "\"uri\":\"cube.scene.textures/color.jpg\"");
        pointer = "/images/0/uri";
    }
    failsAt(fixture, pointer);
}

//======================================================================================================================
TEST_CASE("document content sampler level semantics and references are validated",
          "[asset][scene-document-content]") {
    Fixture fixture;
    SECTION("nearest is single level") {
        replace(fixture.json, "9987", "9728");
        const auto doc = fixture.read();
        REQUIRE(doc);
        CHECK_FALSE(doc->content->images[0].mipmapped);
    }
    SECTION("absent filter means mipmapped") {
        replace(fixture.json, "{\"minFilter\":9987}", "{}");
        const auto doc = fixture.read();
        REQUIRE(doc);
        CHECK(doc->content->images[0].mipmapped);
    }
    SECTION("absent sampler means mipmapped") {
        replace(fixture.json, ",\"sampler\":0", "");
        const auto doc = fixture.read();
        REQUIRE(doc);
        CHECK(doc->content->images[0].mipmapped);
    }
    SECTION("unknown filter") {
        replace(fixture.json, "9987", "9999");
        failsAt(fixture, "/samplers/0/minFilter");
    }
    SECTION("sampler range") {
        replace(fixture.json, "\"sampler\":0", "\"sampler\":2");
        failsAt(fixture, "/textures/0/sampler");
    }
    SECTION("image range") {
        replace(fixture.json, "\"source\":0", "\"source\":2");
        failsAt(fixture, "/textures/0/source");
    }
    SECTION("texture range") {
        replace(fixture.json, "\"index\":0", "\"index\":2");
        failsAt(fixture, "/materials/0/pbrMetallicRoughness/baseColorTexture/index");
    }
    SECTION("conflicting image levels") {
        replace(fixture.json, "\"source\":1", "\"source\":0");
        failsAt(fixture, "/textures/1/sampler");
    }
    SECTION("negative strength") {
        replace(fixture.json, "\"emissiveStrength\":2.5", "\"emissiveStrength\":-1");
        failsAt(fixture,
                "/materials/0/extensions/KHR_materials_emissive_strength/emissiveStrength");
    }
}

//======================================================================================================================
TEST_CASE("document content resolves animation after geometry by URI",
          "[asset][scene-document-content]") {
    Fixture fixture;
    replace(fixture.json, "\"byteLength\":528}]",
            "\"byteLength\":528},{\"uri\":\"cube.scene.bin\",\"byteLength\":32}]");
    replace(fixture.json, "\"target\":34963}]",
            "\"target\":34963},{\"buffer\":1,\"byteLength\":8},{\"buffer\":1,\"byteOffset\":8,"
            "\"byteLength\":24}]");
    replace(
        fixture.json, "\"count\":36,\"type\":\"SCALAR\"}]",
        R"("count":36,"type":"SCALAR"},{"bufferView":2,"componentType":5126,"count":2,"type":"SCALAR","min":[0],"max":[0.01666666753590107]},{"bufferView":3,"componentType":5126,"count":2,"type":"VEC3"}])");
    replace(
        fixture.json, "\"extensions\":{\"LMX_scene\":{\"schemaVersion\"",
        R"("animations":[{"name":"Move","extensions":{"LMX_scene":{"sampleRate":60}},"samplers":[{"input":5,"output":6}],"channels":[{"sampler":0,"target":{"node":1,"path":"translation"}}]}],"extensions":{"LMX_scene":{"schemaVersion")");
    const std::array<float, 8> animation{0.f, 1.f / 60.f, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f};
    writeBytes(fixture.directory / "cube.scene.bin", std::as_bytes(std::span(animation)));
    const auto doc = fixture.read();
    INFO((doc ? "read succeeded" : doc.error().message));
    REQUIRE(doc);
    REQUIRE(doc->animations.size() == 1);
    CHECK(doc->animations[0].channels[0].values[1] == glm::vec4(4.f, 5.f, 6.f, 0.f));
    CHECK(doc->sourceBufferUri == "cube.scene.bin");
    REQUIRE(doc->content->geometries.size() == 1);
}

//======================================================================================================================
TEST_CASE("document content validates buffer shape and exact accessor ranges",
          "[asset][scene-document-content]") {
    Fixture fixture;
    SECTION("empty buffers") {
        replace(fixture.json,
                "\"buffers\":[{\"uri\":\"cube.scene.geometry.bin\",\"byteLength\":528}]",
                "\"buffers\":[]");
        failsAt(fixture, "/buffers");
    }
    SECTION("third buffer") {
        replace(fixture.json, "\"byteLength\":528}]", "\"byteLength\":528},{},{}]");
        failsAt(fixture, "/buffers");
    }
    SECTION("duplicate geometry buffer") {
        replace(fixture.json, "\"byteLength\":528}]",
                "\"byteLength\":528},{\"uri\":\"cube.scene.geometry.bin\",\"byteLength\":528}]");
        failsAt(fixture, "/buffers/1/uri");
    }
    SECTION("empty primitives") {
        replace(fixture.json, "\"primitives\":[{", "\"primitives\":[],\"unused\":[{");
        failsAt(fixture, "/meshes/0/primitives");
    }
    SECTION("wrong vertex shape") {
        replace(fixture.json, "\"type\":\"VEC4\"", "\"type\":\"VEC3\"");
        failsAt(fixture, "/accessors/2/type");
    }
    SECTION("vertex view past buffer") {
        replace(fixture.json, "\"byteLength\":384", "\"byteLength\":1024");
        failsAt(fixture, "/bufferViews/0");
    }
    SECTION("short vertex view") {
        replace(fixture.json, "\"byteLength\":384", "\"byteLength\":380");
        failsAt(fixture, "/accessors/0");
    }
    SECTION("short index view") {
        replace(fixture.json, "\"byteLength\":144", "\"byteLength\":140");
        failsAt(fixture, "/accessors/4");
    }
    SECTION("wrong geometry buffer index") {
        replace(fixture.json, "\"buffer\":0", "\"buffer\":1");
        failsAt(fixture, "/bufferViews/0/buffer");
    }
    SECTION("index past vertices with valid content hash") {
        const auto old = lmx::sha256Hex(fixture.geometry);
        fixture.geometry[384] = std::byte{8};
        replace(fixture.json, old, lmx::sha256Hex(fixture.geometry));
        writeBytes(fixture.directory / "cube.scene.geometry.bin", fixture.geometry);
        failsAt(fixture, "/meshes/0/primitives/0/indices");
    }
    SECTION("nonfinite vertex with valid content hash") {
        const auto old = lmx::sha256Hex(fixture.geometry);
        fixture.geometry[0] = std::byte{0};
        fixture.geometry[1] = std::byte{0};
        fixture.geometry[2] = std::byte{128};
        fixture.geometry[3] = std::byte{127};
        replace(fixture.json, old, lmx::sha256Hex(fixture.geometry));
        writeBytes(fixture.directory / "cube.scene.geometry.bin", fixture.geometry);
        failsAt(fixture, "/meshes/0/primitives/0/attributes");
    }
    SECTION("truncated image with matching hash") {
        const auto path = fixture.directory / "cube.scene.textures/color.png";
        auto bytes = *lmx::readWholeFile(path);
        const auto old = lmx::sha256Hex(bytes);
        bytes.resize(bytes.size() / 2);
        replace(fixture.json, old, lmx::sha256Hex(bytes));
        writeBytes(path, bytes);
        failsAt(fixture, "/images/0/uri");
    }
}

//======================================================================================================================
TEST_CASE("schema 1 remains mesh-free and schema 2 without meshes has no content",
          "[asset][scene-document-content]") {
    const auto original =
        lmx::readWholeFile(fs::path(LMX_REPO_ROOT) / "Tests/Golden/scene-document-min.scene.gltf");
    REQUIRE(original);
    Fixture fixture;
    fixture.json.assign(reinterpret_cast<const char*>(original->data()), original->size());
    SECTION("schema 1 node mesh rejected") {
        replace(fixture.json, "\"camera\": 0", "\"mesh\": 0, \"camera\": 0");
        failsAt(fixture, "/nodes/0/mesh");
    }
    SECTION("schema 2 without geometry") {
        replace(fixture.json, "\"schemaVersion\": 1", "\"schemaVersion\": 2");
        const auto doc = fixture.read();
        REQUIRE(doc);
        CHECK_FALSE(doc->content);
        CHECK(doc->meshes.empty());
        CHECK_FALSE(doc->bounds);
    }
    SECTION("schema 2 empty meshes") {
        replace(fixture.json, "\"schemaVersion\": 1", "\"schemaVersion\": 2");
        replace(fixture.json, "\"asset\":", "\"meshes\": [], \"asset\":");
        const auto doc = fixture.read();
        REQUIRE(doc);
        CHECK_FALSE(doc->content);
    }
    SECTION("historical schema 1 catalog unchanged") {
        const auto catalog = fs::path(LMX_REPO_ROOT) / "Tests/Golden/ux6-schema1-catalog";
        size_t documents = 0;
        for (const auto& file : fs::directory_iterator(catalog)) {
            if (file.path().extension() != ".gltf")
                continue;
            const auto doc = readSceneDocument(file.path());
            REQUIRE(doc);
            CHECK(doc->schemaVersion == 1);
            CHECK_FALSE(doc->content);
            CHECK(doc->meshes.empty());
            const auto bytes = *lmx::readWholeFile(file.path());
            const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            CHECK(sceneDocumentJson(*doc, file.path().stem().string() + ".bin") == text);
            ++documents;
        }
        REQUIRE(documents == 6);
    }
}

//======================================================================================================================
TEST_CASE("schema 2 validates orphan image references and refuses content without meshes",
          "[asset][scene-document-content]") {
    Fixture fixture;
    replace(fixture.json, "\"meshes\":", "\"unusedMeshes\":");
    replace(fixture.json, ",\"mesh\":0", "");
    replace(fixture.json, ",\"mesh\":1", "");
    SECTION("missing file") {
        fs::remove(fixture.directory / "cube.scene.textures/color.png");
        failsAt(fixture, "/images/0/uri");
    }
    SECTION("missing hash") {
        replace(fixture.json, "\"cube.scene.textures/color.png\":", "\"wrong.png\":");
        failsAt(fixture, "/images/0/uri");
    }
    SECTION("verified orphan content is refused rather than dropped") {
        failsAt(fixture, "/materials");
    }
}

//======================================================================================================================
TEST_CASE("content writer round trips geometry materials images and node fields",
          "[asset][scene-document-content][ux6-write]") {
    Fixture fixture;
    auto doc = lmx::test::contentDocument();
    SECTION("geometry alone") {}
    SECTION("animation before geometry") {
        doc.animations = lmx::test::animatedDocument().animations;
    }
    doc.materials[0].values.metallic = std::nextafter(0.3f, 1.f);
    doc.materials[0].values.baseColorFactor.x = std::nextafter(0.2f, 1.f);
    doc.materials[0].emissiveStrength = std::nextafter(2.5f, 3.f);
    REQUIRE(saveSceneDocument(doc, fixture.path));
    const auto read = readSceneDocument(fixture.path);
    REQUIRE(read);
    REQUIRE(read->content);
    REQUIRE(read->meshes.size() == 2);
    CHECK(sceneDocumentJson(doc, "cube.scene.bin") == sceneDocumentJson(*read, "cube.scene.bin"));
    CHECK_FALSE(lmx::scenes::documentDirty(doc, *read));
    CHECK(read->nodes[1].mesh == 0);
    CHECK(read->nodes[2].motion == DocMotion::Invalid);
    CHECK(read->bounds == doc.bounds);
    CHECK(read->materials[0].values.metallic == doc.materials[0].values.metallic);
    CHECK(read->materials[0].values.baseColorFactor == doc.materials[0].values.baseColorFactor);
    CHECK(read->materials[0].emissiveStrength == doc.materials[0].emissiveStrength);
    REQUIRE(read->content->geometries.size() == 1);
    const auto& geo = read->content->geometries[0];
    CHECK(std::memcmp(geo.vertices.data(), fixture.geometry.data(), 384) == 0);
    CHECK(std::memcmp(geo.indices.data(), fixture.geometry.data() + 384, 144) == 0);
    REQUIRE(read->content->images.size() == doc.content->images.size());
    for (size_t i = 0; i < doc.content->images.size(); ++i) {
        CHECK(read->content->images[i].file == doc.content->images[i].file);
        CHECK(read->content->images[i].rgba8 == doc.content->images[i].rgba8);
        CHECK(read->content->images[i].mipmapped == doc.content->images[i].mipmapped);
    }
    const auto first = *lmx::readWholeFile(fixture.path);
    REQUIRE(saveSceneDocument(*read, fixture.path));
    CHECK(*lmx::readWholeFile(fixture.path) == first);
}

//======================================================================================================================
TEST_CASE("content save preserves matching immutable files including read-only companions",
          "[asset][scene-document-content][ux6-write]") {
    Fixture fixture;
    const auto doc = lmx::test::contentDocument();
    const std::array<fs::path, 3> files{fixture.directory / "cube.scene.geometry.bin",
                                        fixture.directory / "cube.scene.textures/color.png",
                                        fixture.directory / "cube.scene.textures/linear.png"};
    const auto time = fs::file_time_type::clock::now() - std::chrono::hours(24);
    for (const auto& file : files) {
        fs::last_write_time(file, time);
        fs::permissions(file, fs::perms::owner_read, fs::perm_options::replace);
    }
    REQUIRE(saveSceneDocument(doc, fixture.path));
    for (const auto& file : files) {
        CHECK((fs::last_write_time(file) == time));
        CHECK(fs::status(file).permissions() == fs::perms::owner_read);
    }
}

//======================================================================================================================
TEST_CASE("content Save As copies a standalone set with encoded companion URIs",
          "[asset][scene-document-content][ux6-write]") {
    Fixture fixture;
    const auto original = fixture.read();
    REQUIRE(original);
    const auto destination = fixture.directory / "copy";
    fs::create_directory(destination);
    const auto path = destination / "copied % cube.scene.gltf";
    REQUIRE(saveSceneDocument(*original, path));
    fs::remove(fixture.path);
    fs::remove(fixture.directory / "cube.scene.geometry.bin");
    fs::remove_all(fixture.directory / "cube.scene.textures");
    const auto read = readSceneDocument(path);
    REQUIRE(read);
    REQUIRE(read->content);
    CHECK_FALSE(lmx::scenes::documentDirty(*original, *read));
    CHECK(*lmx::readWholeFile(destination / "copied % cube.scene.geometry.bin") ==
          fixture.geometry);
    CHECK(read->content->images[0].file == original->content->images[0].file);
}

//======================================================================================================================
TEST_CASE("content save refuses foreign targets and invalid image names before staging",
          "[asset][scene-document-content][ux6-write]") {
    Fixture fixture;
    auto doc = lmx::test::contentDocument();
    std::string namedFile;
    SECTION("mismatching geometry") {
        namedFile = "cube.scene.geometry.bin";
        writeBytes(fixture.directory / namedFile, {});
    }
    SECTION("mismatching texture") {
        namedFile = "color.png";
        writeBytes(fixture.directory / "cube.scene.textures" / namedFile, {});
    }
    SECTION("foreign texture") {
        namedFile = "foreign.txt";
        writeBytes(fixture.directory / "cube.scene.textures" / namedFile, {});
    }
    SECTION("foreign subdirectory") {
        namedFile = "foreign";
        fs::create_directory(fixture.directory / "cube.scene.textures" / namedFile);
    }
    SECTION("unsafe image name") {
        auto content = std::make_shared<DocContent>(*doc.content);
        content->images[0].name = "../escape";
        doc.content = content;
        namedFile = "/images/0/name";
    }
    SECTION("duplicate image name") {
        auto content = std::make_shared<DocContent>(*doc.content);
        content->images[1].name = "color";
        doc.content = content;
        namedFile = "/images/1/name";
    }
    SECTION("case-colliding image name") {
        auto content = std::make_shared<DocContent>(*doc.content);
        content->images[1].name = "COLOR";
        doc.content = content;
        namedFile = "/images/1/name";
    }
    const auto entries =
        std::distance(fs::directory_iterator(fixture.directory), fs::directory_iterator{});
    const auto result = saveSceneDocument(doc, fixture.path);
    REQUIRE_FALSE(result);
    CHECK(result.error().message.contains(namedFile));
    CHECK_FALSE(fs::exists(fixture.path));
    CHECK(std::distance(fs::directory_iterator(fixture.directory), fs::directory_iterator{}) ==
          entries);
}

//======================================================================================================================
TEST_CASE("trusted content validation skips only the immutable byte checks",
          "[asset][scene-document-content][ux6-write]") {
    auto doc = lmx::test::contentDocument();
    REQUIRE(validateSceneDocumentModel(doc));
    auto content = std::make_shared<DocContent>(*doc.content);
    content->geometrySha256 = std::string(64, '0');
    content->images[0].sha256 = std::string(64, '0');
    doc.content = content;
    const auto verified = validateSceneDocumentModel(doc);
    REQUIRE_FALSE(verified);
    CHECK(verified.error().message.contains("/extensions/LMX_scene/contentHashes"));
    CHECK(validateSceneDocumentModel(doc, ContentBytes::Trusted));
    doc.meshes[0].geometry = 7;
    CHECK_FALSE(validateSceneDocumentModel(doc, ContentBytes::Trusted));
}

//======================================================================================================================
TEST_CASE("content save ignores hidden entries in the texture folder",
          "[asset][scene-document-content][ux6-write]") {
    Fixture fixture;
    const auto doc = lmx::test::contentDocument();
    writeBytes(fixture.directory / "cube.scene.textures" / ".DS_Store", {});
    REQUIRE(saveSceneDocument(doc, fixture.path));
    CHECK(fs::exists(fixture.directory / "cube.scene.textures" / ".DS_Store"));
}

//======================================================================================================================
TEST_CASE("content save rolls back new companions at every rename failure",
          "[asset][scene-document-content][ux6-write]") {
    for (unsigned failAt = 1; failAt <= 5; ++failAt) {
        Fixture fixture;
        auto doc = lmx::test::contentDocument();
        doc.animations = lmx::test::animatedDocument().animations;
        fs::remove_all(fixture.directory / "cube.scene.textures");
        fs::remove(fixture.directory / "cube.scene.geometry.bin");
        unsigned calls = 0;
        const auto result = detail::saveSceneDocumentWithRename(
            doc, fixture.path,
            [&](const fs::path& from, const fs::path& to, std::error_code& error) {
                if (++calls == failAt)
                    error = std::make_error_code(std::errc::io_error);
                else
                    fs::rename(from, to, error);
            });
        INFO(failAt);
        REQUIRE_FALSE(result);
        CHECK(calls >= failAt);
        CHECK(fs::is_empty(fixture.directory));
    }
}

//======================================================================================================================
TEST_CASE("schema 2 hash and buffer discovery use only the animation companion",
          "[asset][scene-document-content][ux6-write]") {
    Fixture fixture;
    auto doc = lmx::test::contentDocument();
    SECTION("geometry alone") {}
    SECTION("both buffers") {
        doc.animations = lmx::test::animatedDocument().animations;
    }
    REQUIRE(saveSceneDocument(doc, fixture.path));
    const auto bytes = *lmx::readWholeFile(fixture.path);
    const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    const auto animation = sceneDocumentBuffer(doc);
    auto expected = bytes;
    expected.insert(expected.end(), animation.begin(), animation.end());
    const auto hash = sceneDocumentHash(fixture.path);
    REQUIRE(hash);
    CHECK(*hash == lmx::sha256Hex(expected));
    const auto buffer = sceneDocumentBufferPath(text, fixture.path);
    REQUIRE(buffer);
    CHECK(buffer->has_value() == !animation.empty());
    if (*buffer)
        CHECK(**buffer == fixture.directory / "cube.scene.bin");
}

//======================================================================================================================
TEST_CASE("document geometry serialization is stable in geometry order independent of mesh order",
          "[asset][scene-document-content][ux6-write][ux6-layout]") {
    Fixture fixture;
    auto doc = lmx::test::contentDocument();
    auto content = std::make_shared<DocContent>(*doc.content);
    content->geometries.push_back(content->geometries[0]);
    content->geometries[1].vertices[0].px = -2.f;
    doc.content = content;
    doc.meshes[0].geometry = 1;
    content->geometrySha256 = lmx::sha256Hex(sceneDocumentGeometry(doc));
    auto expected = fixture.geometry;
    auto second = fixture.geometry;
    const float value = -2.f;
    std::memcpy(second.data(), &value, sizeof(value));
    expected.insert(expected.end(), second.begin(), second.end());
    CHECK(sceneDocumentGeometry(doc) == expected);
    CHECK(sceneDocumentGeometry(doc) == sceneDocumentGeometry(doc));
    CHECK(sceneDocumentGeometry(SceneDocument{}).empty());
    fs::remove(fixture.directory / "cube.scene.geometry.bin");
    REQUIRE(saveSceneDocument(doc, fixture.path));
    const auto read = readSceneDocument(fixture.path);
    REQUIRE(read);
    CHECK_FALSE(lmx::scenes::documentDirty(doc, *read));
    CHECK(sceneDocumentGeometry(*read) == expected);
    const auto again = saveSceneDocument(*read, fixture.path);
    INFO((again ? "saved" : again.error().message));
    REQUIRE(again);
}

//======================================================================================================================
TEST_CASE("noncanonical geometry bytes fail at the geometry URI before a document is returned",
          "[asset][scene-document-content][ux6-write][ux6-layout]") {
    Fixture fixture;
    const auto originalHash = lmx::sha256Hex(fixture.geometry);
    SECTION("unused trailing bytes") {
        fixture.geometry.resize(fixture.geometry.size() + 4);
    }
    SECTION("leading padding") {
        fixture.geometry.insert(fixture.geometry.begin(), 4, std::byte{});
        replace(fixture.json, "\"byteOffset\":0,\"byteLength\":384",
                "\"byteOffset\":4,\"byteLength\":384");
        replace(fixture.json, "\"byteOffset\":384", "\"byteOffset\":388");
    }
    replace(fixture.json, "\"byteLength\":528", "\"byteLength\":532");
    replace(fixture.json, originalHash, lmx::sha256Hex(fixture.geometry));
    writeBytes(fixture.directory / "cube.scene.geometry.bin", fixture.geometry);
    failsAt(fixture, "/buffers/0/uri");
}

//======================================================================================================================
TEST_CASE("schema 2 image names and paths must preserve immutable save filenames",
          "[asset][scene-document-content][ux6-write][ux6-layout]") {
    Fixture fixture;
    std::string pointer = "/images/0/name";
    SECTION("name differs from filename") {
        replace(fixture.json, "\"name\":\"color\"", "\"name\":\"Color Image\"");
    }
    SECTION("unsafe name") {
        replace(fixture.json, "\"name\":\"color\"", "\"name\":\"../color\"");
    }
    SECTION("duplicate name") {
        replace(fixture.json, "\"name\":\"linear\"", "\"name\":\"color\"");
        pointer = "/images/1/name";
    }
    SECTION("nested image") {
        const auto nested = fixture.directory / "cube.scene.textures/nested";
        fs::create_directory(nested);
        fs::rename(fixture.directory / "cube.scene.textures/color.png", nested / "color.png");
        replace(fixture.json, "\"uri\":\"cube.scene.textures/color.png\"",
                "\"uri\":\"cube.scene.textures/nested/color.png\"");
        replace(fixture.json,
                "\"cube.scene.textures/color.png\":", "\"cube.scene.textures/nested/color.png\":");
        pointer = "/images/0/uri";
    }
    failsAt(fixture, pointer);
}

//======================================================================================================================
TEST_CASE("document hash retains animation length checks without reading immutable content",
          "[asset][scene-document-content][ux6-write][ux6-hash]") {
    Fixture fixture;
    auto doc = lmx::test::contentDocument();
    doc.animations = lmx::test::animatedDocument().animations;
    SECTION("schema 1") {
        doc = lmx::test::animatedDocument();
    }
    SECTION("schema 2") {}
    REQUIRE(saveSceneDocument(doc, fixture.path));
    const auto jsonBytes = *lmx::readWholeFile(fixture.path);
    std::string json(reinterpret_cast<const char*>(jsonBytes.data()), jsonBytes.size());
    writeBytes(fixture.directory / "cube.scene.bin", {});
    CHECK_FALSE(sceneDocumentHash(fixture.path));
    writeBytes(fixture.directory / "cube.scene.bin", sceneDocumentBuffer(doc));
    replace(json, "\"byteLength\": 32", "\"byteLength\": 0");
    writeBytes(fixture.path, std::as_bytes(std::span(json)));
    CHECK_FALSE(sceneDocumentHash(fixture.path));
    replace(json, "\"byteLength\": 0", "\"byteLength\": -1");
    writeBytes(fixture.path, std::as_bytes(std::span(json)));
    CHECK_FALSE(sceneDocumentHash(fixture.path));
    writeBytes(fixture.path, jsonBytes);
    const auto before = sceneDocumentHash(fixture.path);
    REQUIRE(before);
    fs::remove(fixture.directory / "cube.scene.geometry.bin");
    fs::remove_all(fixture.directory / "cube.scene.textures");
    const auto after = sceneDocumentHash(fixture.path);
    REQUIRE(after);
    CHECK(*after == *before);
}

//======================================================================================================================
TEST_CASE("content save rollback restores the old document pair and removes new companions",
          "[asset][scene-document-content][ux6-write]") {
    for (unsigned failAt = 1; failAt <= 7; ++failAt) {
        Fixture fixture;
        fs::remove_all(fixture.directory / "cube.scene.textures");
        fs::remove(fixture.directory / "cube.scene.geometry.bin");
        const auto old = lmx::test::animatedDocument();
        REQUIRE(saveSceneDocument(old, fixture.path));
        const auto oldJson = *lmx::readWholeFile(fixture.path);
        const auto oldBin = *lmx::readWholeFile(fixture.directory / "cube.scene.bin");
        auto doc = lmx::test::contentDocument();
        doc.animations = old.animations;
        unsigned calls = 0;
        const auto result = detail::saveSceneDocumentWithRename(
            doc, fixture.path,
            [&](const fs::path& from, const fs::path& to, std::error_code& error) {
                if (++calls == failAt)
                    error = std::make_error_code(std::errc::io_error);
                else
                    fs::rename(from, to, error);
            });
        INFO(failAt);
        REQUIRE_FALSE(result);
        CHECK(calls >= failAt);
        CHECK(*lmx::readWholeFile(fixture.path) == oldJson);
        CHECK(*lmx::readWholeFile(fixture.directory / "cube.scene.bin") == oldBin);
        CHECK(std::distance(fs::directory_iterator(fixture.directory), fs::directory_iterator{}) ==
              2);
    }
}
