// FrameDataBench: the M5.2 production frame-data benchmark (Task 1B). Times the RHI's per-frame
// data-delivery path -- today the incumbent CommandList::setUniforms, reached through this
// benchmark's one delivery seam (DeliverPerDrawData.h) -- over the five frozen workloads in
// docs/specs/2026-08-12-m5.2-rhi-frame-data-design.md section 11. Renders offscreen only: no
// window, no ImGui, no scene assets, just deterministic synthetic quads and a readback digest.
//
// Usage: FrameDataBench --case <name> --out <path.json> [--verify] [--warmup N] [--frames N]
//   --case     one of F-FIT-512, F-DYNAMIC-1024, F-DYNAMIC-4096, F-STATIC-1024, F-STATIC-4096.
//   --out      path the result JSON is written to (parent directories are created).
//   --verify   reads back the final measured frame and reports its FNV-1a digest, so a paired
//              driver can refuse to compare timing when a baseline and candidate build disagree.
//   --warmup   frames run and discarded before timing starts (default 16).
//   --frames   frames whose timed region is recorded (default 256).
#include "Digest.h"
#include "Runner.h"
#include "Workload.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct CliOptions {
    std::string caseName;
    std::filesystem::path outPath;
    bool verify = false;
    uint32_t warmupFrames = 16;
    uint32_t measuredFrames = 256;
};

//======================================================================================================================
std::optional<CliOptions> parseArgs(const std::vector<std::string_view>& args, std::string& error) {
    CliOptions options;
    bool sawCase = false;
    bool sawOut = false;

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        const auto needValue = [&](std::string_view flag) -> std::optional<std::string_view> {
            if (++i >= args.size()) {
                error = std::string(flag) + " needs a value";
                return std::nullopt;
            }
            return args[i];
        };
        if (arg == "--case") {
            const auto value = needValue(arg);
            if (!value) {
                return std::nullopt;
            }
            options.caseName = *value;
            sawCase = true;
        } else if (arg == "--out") {
            const auto value = needValue(arg);
            if (!value) {
                return std::nullopt;
            }
            options.outPath = *value;
            sawOut = true;
        } else if (arg == "--verify") {
            options.verify = true;
        } else if (arg == "--warmup") {
            const auto value = needValue(arg);
            if (!value) {
                return std::nullopt;
            }
            options.warmupFrames = static_cast<uint32_t>(std::stoul(std::string(*value)));
        } else if (arg == "--frames") {
            const auto value = needValue(arg);
            if (!value) {
                return std::nullopt;
            }
            options.measuredFrames = static_cast<uint32_t>(std::stoul(std::string(*value)));
        } else {
            error = "unknown argument '" + std::string(arg) + "'";
            return std::nullopt;
        }
    }

    if (!sawCase) {
        error = "--case is required";
        return std::nullopt;
    }
    if (!sawOut) {
        error = "--out is required";
        return std::nullopt;
    }
    return options;
}

//======================================================================================================================
// Matches Tools/TextureBake and Experiments/NoApi's hand-rolled JSON precedent -- escapes the two
// characters JSON requires plus control characters, no third-party dependency.
std::string jsonEscape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            char buffer[7];
            std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
            out += buffer;
        } else {
            out += c;
        }
    }
    return out;
}

//======================================================================================================================
bool writeResultJson(const std::filesystem::path& path, const CliOptions& options,
                     const lmx::bench::RunResult& result) {
    std::error_code errorCode;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), errorCode);
    }
    std::ofstream file(path, std::ios::trunc);
    if (!file) {
        std::fprintf(stderr, "FrameDataBench: cannot open '%s' for writing\n", path.string().c_str());
        return false;
    }
    file << "{\n";
    file << "  \"case\": \"" << jsonEscape(options.caseName) << "\",\n";
    file << "  \"warmupFrames\": " << options.warmupFrames << ",\n";
    file << "  \"measuredFrames\": " << options.measuredFrames << ",\n";
    file << "  \"perFrameTimedRegionNs\": [";
    for (size_t i = 0; i < result.perFrameTimedRegionNs.size(); ++i) {
        file << (i == 0 ? "" : ",") << result.perFrameTimedRegionNs[i];
    }
    file << "],\n";
    file << "  \"medianNs\": " << result.medianNs << ",\n";
    file << "  \"verify\": " << (options.verify ? "true" : "false") << ",\n";
    if (result.digest) {
        file << "  \"digest\": \"" << lmx::bench::digestToHex(*result.digest) << "\"\n";
    } else {
        file << "  \"digest\": null\n";
    }
    file << "}\n";
    return static_cast<bool>(file);
}

} // namespace

//======================================================================================================================
int main(int argc, char** argv) {
    std::vector<std::string_view> args(argv + (argc > 0 ? 1 : 0), argv + argc);
    std::string parseError;
    const std::optional<CliOptions> options = parseArgs(args, parseError);
    if (!options) {
        std::fprintf(stderr,
                     "FrameDataBench: %s\n"
                     "usage: FrameDataBench --case <name> --out <path.json> [--verify] "
                     "[--warmup N] [--frames N]\n",
                     parseError.c_str());
        return 2;
    }

    const lmx::bench::WorkloadSpec* spec = lmx::bench::findWorkload(options->caseName);
    if (spec == nullptr) {
        std::fprintf(stderr, "FrameDataBench: unknown --case '%s'; expected one of:",
                     options->caseName.c_str());
        for (const lmx::bench::WorkloadSpec& candidate : lmx::bench::kWorkloads) {
            std::fprintf(stderr, " %s", std::string(candidate.name).c_str());
        }
        std::fprintf(stderr, "\n");
        return 2;
    }

    const lmx::bench::RunConfig config{.warmupFrames = options->warmupFrames,
                                       .measuredFrames = options->measuredFrames,
                                       .verify = options->verify};
    const lmx::bench::RunResult result = lmx::bench::runWorkload(*spec, config);
    if (!result.ok) {
        std::fprintf(stderr, "FrameDataBench: case '%s' failed: %s\n", options->caseName.c_str(),
                     result.error.c_str());
        return 1;
    }

    if (!writeResultJson(options->outPath, *options, result)) {
        return 1;
    }

    std::printf("case=%s medianNs=%llu digest=%s\n", options->caseName.c_str(),
               static_cast<unsigned long long>(result.medianNs),
               result.digest ? lmx::bench::digestToHex(*result.digest).c_str() : "(none)");
    return 0;
}
