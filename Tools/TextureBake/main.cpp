// Thin CLI wrapper around Source/Engine/TextureBake.{h,cpp} -- decodes one PNG/JPG, bakes a full
// linear-light box-filtered mip chain, and writes it as an uncompressed A8R8G8B8 DDS plus a
// sibling "<out>.dds.json" manifest. All the deterministic math lives in Engine so unit tests
// exercise it directly; this file only does argv parsing and file I/O.
//
// Usage: TextureBake <in.png|jpg> <out.dds> --srgb|--linear|--normal-map [--source-name NAME]
//   --srgb        base-color images: decode sRGB -> linear, filter, re-encode sRGB.
//   --linear      data images already linear: filter raw bytes, no colour-space transform.
//   --normal-map  tangent-space normals: decode to [-1,1], filter, renormalize, round-nearest.
//   --source-name overrides the manifest's recorded "source" string (default: <in> verbatim) --
//                 for an embedded glTF image baked from a temp extraction file, the caller passes
//                 a human-meaningful identifier instead of the throwaway temp path.
#include "Engine/TextureBake.h"

#include <stb/stb_image.h>

#include <cstdio>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr std::string_view kToolVersion = "1";

//======================================================================================================================
std::optional<std::vector<std::byte>> readFile(const std::string& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return std::nullopt;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) {
        return std::nullopt;
    }
    std::vector<std::byte> bytes(static_cast<size_t>(size));
    in.seekg(0, std::ios::beg);
    in.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!in) {
        return std::nullopt;
    }
    return bytes;
}

} // namespace

//======================================================================================================================
int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.size() < 3) {
        std::fprintf(stderr,
                     "usage: TextureBake <in.png|jpg> <out.dds> --srgb|--linear|--normal-map "
                     "[--source-name NAME]\n");
        return 2;
    }

    const std::string inPath = args[0];
    const std::string outPath = args[1];
    std::optional<lmx::engine::BakeMode> mode;
    std::string sourceName = inPath;
    for (size_t i = 2; i < args.size(); ++i) {
        if (args[i] == "--srgb") {
            mode = lmx::engine::BakeMode::Srgb;
        } else if (args[i] == "--linear") {
            mode = lmx::engine::BakeMode::Linear;
        } else if (args[i] == "--normal-map") {
            mode = lmx::engine::BakeMode::NormalMap;
        } else if (args[i] == "--source-name" && i + 1 < args.size()) {
            sourceName = args[++i];
        } else {
            std::fprintf(stderr, "TextureBake: unrecognized argument '%s'\n", args[i].c_str());
            return 2;
        }
    }
    if (!mode) {
        std::fprintf(stderr,
                     "TextureBake: exactly one of --srgb, --linear, --normal-map is required\n");
        return 2;
    }

    const std::optional<std::vector<std::byte>> sourceBytes = readFile(inPath);
    if (!sourceBytes) {
        std::fprintf(stderr, "TextureBake: failed to read '%s'\n", inPath.c_str());
        return 1;
    }

    int width = 0, height = 0, channels = 0;
    unsigned char* pixels =
        stbi_load_from_memory(reinterpret_cast<const unsigned char*>(sourceBytes->data()),
                              static_cast<int>(sourceBytes->size()), &width, &height, &channels, 4);
    if (pixels == nullptr) {
        std::fprintf(stderr, "TextureBake: stb_image failed to decode '%s': %s\n", inPath.c_str(),
                     stbi_failure_reason());
        return 1;
    }

    const std::span<const uint8_t> rgba8(pixels, static_cast<size_t>(width) * height * 4);
    const lmx::engine::BakedMipChain baked = lmx::engine::bakeMips(
        rgba8, static_cast<uint32_t>(width), static_cast<uint32_t>(height), *mode);
    stbi_image_free(pixels);

    if (auto written = lmx::engine::writeDds(outPath, baked); !written) {
        std::fprintf(stderr, "TextureBake: %s\n", written.error().message.c_str());
        return 1;
    }

    const std::string sourceHash = lmx::engine::sha256Hex(*sourceBytes);
    const std::string manifestPath = outPath + ".json";
    if (auto written =
            lmx::engine::writeManifest(manifestPath, sourceName, sourceHash, *mode, kToolVersion);
        !written) {
        std::fprintf(stderr, "TextureBake: %s\n", written.error().message.c_str());
        return 1;
    }

    std::printf("baked %s -> %s (%dx%d, %u mip levels)\n", inPath.c_str(), outPath.c_str(), width,
               height, baked.mipLevels);
    return 0;
}
