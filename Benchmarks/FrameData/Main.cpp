//----------------------------------------------------------------------------------------------------------------------
/// @file Main.cpp
/// @brief Implements the FrameDataBench command-line entry point.
//----------------------------------------------------------------------------------------------------------------------

/// @details FrameDataBench: the production frame-data benchmark. Times the RHI's
///        per-frame data-delivery path -- CommandList::bindFrameData, reached through this
///        benchmark's one delivery seam (DeliverPerDrawData.h) -- over the five frozen workloads in
///        docs/specs/2026-08-12-m5.2-rhi-frame-data-design.md section 11. The paired driver's
///        baseline side builds this same seam from the frozen `m5.2-baseline` tag, where the
///        delivery seam still reaches the incumbent RHI's transient-uniform delivery path.
///        Renders offscreen only: no window, no ImGui, no scene assets, just deterministic
///        synthetic quads and a readback digest.
///
///        Usage: FrameDataBench --case <name> --out <path.json> [--verify] [--warmup N] [--frames
///        N]
///          --case     one of F-FIT-512, F-DYNAMIC-1024, F-DYNAMIC-4096, F-STATIC-1024,
///                     F-STATIC-4096.
///          --out      path the result JSON is written to (parent directories are created).
///          --verify   reads back the final measured frame and reports its FNV-1a digest, so a
///                     paired driver can refuse to compare timing when a baseline and candidate
///                     build disagree.
///          --warmup   frames run and discarded before timing starts (default 16).
///          --frames   frames whose timed region is recorded (default 256).
///
///        Usage: FrameDataBench --selftest
///          Runs this binary's own pure host-side logic checks (no GPU device touched) and exits
///          0 iff every check passed; failures print to stderr.
#include "DeliverPerDrawData.h"
#include "Digest.h"
#include "Runner.h"
#include "Workload.h"

#include "RHI/Metal4/Metal4FrameData.h"

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
// Matches Tools/TextureBake's hand-rolled JSON precedent: escapes the two characters JSON requires
// plus control characters, with no third-party dependency.
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
// Serializes one FrameDataCounters snapshot (candidate-only counter evidence) as a JSON
// object literal, or writes `null` when the run never took this snapshot (RunResult's
// std::optional is unset -- see Runner.h). The baseline binary this JSON schema also serves is
// built from the frozen `m5.2-baseline` tag tree, whose copy of Main.cpp has none of this code at
// all, so a missing "frameData" key -- not a null field inside one -- is how a baseline JSON
// differs; the paired driver never reads these additive fields, so either shape is backward
// compatible with it.
void writeFrameDataCounters(std::ofstream& file,
                            const std::optional<lmx::rhi::metal4::FrameDataCounters>& counters) {
    if (!counters) {
        file << "null";
        return;
    }
    file << "{\"calls\": " << counters->calls << ", \"bytes\": " << counters->bytes
         << ", \"addressBinds\": " << counters->addressBinds
         << ", \"pageCreations\": " << counters->pageCreations << ", \"slots\": [";
    for (size_t i = 0; i < counters->slots.size(); ++i) {
        const auto& slot = counters->slots[i];
        file << (i == 0 ? "" : ", ") << "{\"pageCount\": " << slot.pageCount
             << ", \"pagesUsed\": " << slot.pagesUsed << ", \"bytesUsed\": " << slot.bytesUsed
             << ", \"capacityBytes\": " << slot.capacityBytes << "}";
    }
    file << "]}";
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
        std::fprintf(stderr, "FrameDataBench: cannot open '%s' for writing\n",
                     path.string().c_str());
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
    file << "  \"overflowBufferCreations\": " << result.overflowBufferCreations << ",\n";
    file << "  \"overflowBufferCreationsPerFrame\": " << result.overflowBufferCreationsPerFrame
         << ",\n";
    // The exact client-side ring model this run's overflow decisions assumed
    // (DeliverPerDrawData.h); recorded so a later reader never has to re-derive or guess the
    // constants a baseline result was built on.
    file << "  \"ringModel\": {\"capacityBytes\": " << lmx::bench::kRingCapacityBytes
         << ", \"alignmentBytes\": " << lmx::bench::kRingAlignmentBytes << "},\n";
    file << "  \"verify\": " << (options.verify ? "true" : "false") << ",\n";
    if (result.digest) {
        file << "  \"digest\": \"" << lmx::bench::digestToHex(*result.digest) << "\",\n";
    } else {
        file << "  \"digest\": null,\n";
    }
    // Candidate-only counter evidence (additive; absent from a baseline JSON built from the frozen
    // m5.2-baseline tag tree -- see writeFrameDataCounters above).
    file << "  \"frameDataCountersAfterWarmup\": ";
    writeFrameDataCounters(file, result.frameDataCountersAfterWarmup);
    file << ",\n";
    file << "  \"frameDataCountersAfterMeasurement\": ";
    writeFrameDataCounters(file, result.frameDataCountersAfterMeasurement);
    file << "\n";
    file << "}\n";
    return static_cast<bool>(file);
}

} // namespace

//======================================================================================================================
int main(int argc, char** argv) {
    std::vector<std::string_view> args(argv + (argc > 0 ? 1 : 0), argv + argc);

    // --selftest is a distinct mode (no GPU device, no --case/--out) recognized before the
    // ordinary flag parser, which would otherwise reject it for lacking those required flags.
    for (const std::string_view arg : args) {
        if (arg != "--selftest") {
            continue;
        }
        std::vector<std::string> failures;
        if (lmx::bench::runSelfTests(failures)) {
            std::printf("FrameDataBench --selftest: all checks passed\n");
            return 0;
        }
        std::fprintf(stderr, "FrameDataBench --selftest: %zu check(s) failed:\n", failures.size());
        for (const std::string& failure : failures) {
            std::fprintf(stderr, "  - %s\n", failure.c_str());
        }
        return 1;
    }

    std::string parseError;
    const std::optional<CliOptions> options = parseArgs(args, parseError);
    if (!options) {
        std::fprintf(stderr,
                     "FrameDataBench: %s\n"
                     "usage: FrameDataBench --case <name> --out <path.json> [--verify] "
                     "[--warmup N] [--frames N]\n"
                     "       FrameDataBench --selftest\n",
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

    std::printf("case=%s medianNs=%llu overflowBufferCreations=%llu digest=%s\n",
                options->caseName.c_str(), static_cast<unsigned long long>(result.medianNs),
                static_cast<unsigned long long>(result.overflowBufferCreations),
                result.digest ? lmx::bench::digestToHex(*result.digest).c_str() : "(none)");
    return 0;
}
