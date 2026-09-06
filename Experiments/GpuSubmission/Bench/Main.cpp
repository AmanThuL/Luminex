//----------------------------------------------------------------------------------------------------------------------
/// @file Main.cpp
/// @brief Runs isolated submission experiments and writes reproducible evidence bundles.
//----------------------------------------------------------------------------------------------------------------------
#include "Metal/Host.h"
#include "Reference/RhiReference.h"

#include <CommonCrypto/CommonDigest.h>
#include <mach-o/dyld.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace sub = lmx::experimental::submission;
namespace fs = std::filesystem;
namespace {

//======================================================================================================================
std::string quote(std::string_view value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\')
            out << '\\' << c;
        else if (c < 32)
            out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c);
        else
            out << c;
    }
    out << '"';
    return out.str();
}

//======================================================================================================================
std::string hash(std::string_view data) {
    CC_SHA256_CTX context;
    CC_SHA256_Init(&context);
    size_t offset = 0;
    while (offset < data.size()) {
        const auto count = static_cast<CC_LONG>(std::min<size_t>(65536, data.size() - offset));
        CC_SHA256_Update(&context, data.data() + offset, count);
        offset += count;
    }
    unsigned char bytes[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(bytes, &context);
    std::ostringstream out;
    for (auto b : bytes)
        out << std::hex << std::setw(2) << std::setfill('0') << unsigned(b);
    return out.str();
}

//======================================================================================================================
std::string read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("cannot read " + path.string());
    return {std::istreambuf_iterator<char>(input), {}};
}

//======================================================================================================================
void write(const fs::path& path, std::string_view data) {
    std::ofstream output(path, std::ios::binary);
    if (!output)
        throw std::runtime_error("cannot write " + path.string());
    output.write(data.data(), static_cast<std::streamsize>(data.size()));
    output.flush();
    if (!output)
        throw std::runtime_error("incomplete write " + path.string());
}

//======================================================================================================================
fs::path executable() {
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size))
        throw std::runtime_error("executable path unavailable");
    return fs::canonical(buffer.data());
}

//======================================================================================================================
bool enabled(const char* key) {
    const char* value = std::getenv(key);
    return value && std::string_view(value) != "0" && *value;
}

//======================================================================================================================
std::string environment() {
    utsname info{};
    if (uname(&info))
        throw std::runtime_error("cannot inspect host environment");
    std::array<char, 256> model{};
    size_t length = model.size();
    sysctlbyname("hw.model", model.data(), &length, nullptr, 0);
    std::ostringstream out;
    out << "{\"osRelease\":" << quote(info.release) << ",\"osVersion\":" << quote(info.version)
        << ",\"machine\":" << quote(info.machine) << ",\"hostModel\":" << quote(model.data())
        << ",\"compiler\":" << quote(__clang_version__)
        << ",\"validation\":" << (enabled("MTL_DEBUG_LAYER") ? "true" : "false")
        << ",\"capture\":" << (enabled("MTL_CAPTURE_ENABLED") ? "true" : "false")
        << ",\"shaderValidation\":" << (enabled("MTL_SHADER_VALIDATION") ? "true" : "false")
        << ",\"powerSource\":null,\"thermalState\":null}";
    return out.str();
}

struct Options {
    std::string action, caseId = "all", suite = "all", pair = "gpu-args,direct", order = "AB";
    std::string variant = "all";
    std::string diagnosticDependency;
    sub::Lane lane = sub::Lane::Headline;
    sub::RunConfig config;
    fs::path output;
};

//======================================================================================================================
uint32_t count(std::string_view value) {
    uint32_t result = 0;
    auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size())
        throw std::runtime_error("invalid nonnegative integer");
    return result;
}

//======================================================================================================================
Options parse(int argc, char** argv) {
    Options options;
    options.config.shaderDirectory = executable().parent_path() / "Shaders";
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        if (key == "--measure" || key == "--verify" || key == "--capabilities" ||
            key == "--list-cases" || key == "--selftest" || key == "--diagnose") {
            if (!options.action.empty())
                throw std::runtime_error("choose exactly one action");
            options.action = key;
            continue;
        }
        if (++i == argc)
            throw std::runtime_error("missing value for " + key);
        const std::string value = argv[i];
        if (key == "--output")
            options.output = fs::absolute(value);
        else if (key == "--case")
            options.caseId = value;
        else if (key == "--suite")
            options.suite = value;
        else if (key == "--pair")
            options.pair = value;
        else if (key == "--order")
            options.order = value;
        else if (key == "--variant")
            options.variant = value;
        else if (key == "--diagnostic-dependency") {
            if (value != "declared" && value != "all")
                throw std::runtime_error("diagnostic dependency must be declared or all");
            options.diagnosticDependency = value;
        } else if (key == "--lane") {
            auto lane = sub::parseLane(value);
            if (!lane)
                throw std::runtime_error(lane.error());
            options.lane = *lane;
        } else if (key == "--frames")
            options.config.frames = count(value);
        else if (key == "--warmup")
            options.config.warmup = count(value);
        else if (key == "--shaders")
            options.config.shaderDirectory = fs::canonical(value);
        else if (key == "--capture")
            options.config.capturePath = fs::absolute(value);
        else
            throw std::runtime_error("unknown option " + key);
    }
    if (options.action.empty())
        throw std::runtime_error(
            "expected --capabilities, --list-cases, --selftest, --verify, --measure or --diagnose");
    if (!options.diagnosticDependency.empty() &&
        (options.action != "--diagnose" ||
         (options.diagnosticDependency != "declared" && options.diagnosticDependency != "all")))
        throw std::runtime_error("diagnostic dependency requires --diagnose and declared or all");
    if (!options.config.frames)
        throw std::runtime_error("frames must be positive");
    if (options.order != "AB" && options.order != "BA")
        throw std::runtime_error("order must be AB or BA");
    return options;
}

//======================================================================================================================
std::string caseJson(const sub::Case& spec) {
    std::ostringstream out;
    out << "{\"id\":" << quote(spec.id) << ",\"count\":" << spec.count
        << ",\"triangles\":" << spec.triangles << ",\"bins\":" << spec.bins
        << ",\"visibleFraction\":" << std::setprecision(17) << spec.visibleFraction << '}';
    return out.str();
}

//======================================================================================================================
std::string number(const std::optional<double>& value) {
    if (!value)
        return "null";
    if (!std::isfinite(*value))
        throw std::runtime_error("non-finite timing");
    std::ostringstream out;
    out << std::setprecision(17) << *value;
    return out.str();
}

//======================================================================================================================
std::string samplesJson(const sub::RunResult& run) {
    std::ostringstream out;
    out << '[';
    for (size_t i = 0; i < run.samples.size(); ++i) {
        if (i)
            out << ',';
        const auto& sample = run.samples[i];
        out << "{\"frame\":" << sample.frame << ",\"cpuWorkNs\":" << sample.cpuWorkNs
            << ",\"waitNs\":" << sample.waitNs << ",\"drawCalls\":" << sample.drawCalls
            << ",\"copiedBytes\":" << sample.copiedBytes
            << ",\"gpuSpanMs\":" << number(sample.gpuSpanMs)
            << ",\"preparationMs\":" << number(sample.preparationMs)
            << ",\"rasterMs\":" << number(sample.rasterMs) << '}';
    }
    out << ']';
    return out.str();
}

//======================================================================================================================
std::string shaderHash(const fs::path& directory) {
    std::string data;
    for (const char* stem : {"Scene", "Prepare"}) {
        for (const char* extension : {".metal", ".metallib"}) {
            const auto path = directory / (std::string(stem) + extension);
            if (fs::exists(path))
                data += path.filename().string() + hash(read(path));
            else if (std::string_view(extension) == ".metal")
                throw std::runtime_error("missing shader " + path.string());
        }
    }
    return hash(data);
}

//======================================================================================================================
void prepareOutput(const fs::path& path) {
    if (path.empty())
        throw std::runtime_error("--output is required");
    if (fs::exists(path))
        throw std::runtime_error("output already exists: " + path.string());
    fs::create_directories(path.parent_path());
    if (!fs::create_directory(path))
        throw std::runtime_error("cannot reserve output directory");
}

//======================================================================================================================
std::string identities(const fs::path& shaders) {
    return "\"shaderHash\":" + quote(shaderHash(shaders)) +
           ",\"executableHash\":" + quote(hash(read(executable()))) + ",\"protocolHash\":" +
           quote(hash("submission-v1:seed4c4d5836:32warm:256frames:12pairs:10000bootstrap:15pct:"
                      "ci95:guard-15"));
}

//======================================================================================================================
void measure(const Options& options) {
    if (enabled("MTL_DEBUG_LAYER") || enabled("MTL_CAPTURE_ENABLED") ||
        enabled("MTL_SHADER_VALIDATION") || !options.config.capturePath.empty())
        throw std::runtime_error("measurement refuses validation/capture; use --verify");
    auto spec = sub::findCase(options.caseId);
    auto suite = sub::parseSuite(options.suite);
    if (!spec || !suite)
        throw std::runtime_error(!spec ? spec.error() : suite.error());
    const auto separator = options.pair.find(',');
    if (separator == std::string::npos)
        throw std::runtime_error("pair must be candidate,control");
    auto a = sub::parseVariant(options.pair.substr(0, separator));
    auto b = sub::parseVariant(options.pair.substr(separator + 1));
    if (!a || !b || *a == *b)
        throw std::runtime_error("invalid candidate/control pair");
    prepareOutput(options.output);
    const auto manifest = sub::manifestJson(*spec);
    write(options.output / "manifest.json", manifest);
    const auto env = environment();
    std::ostringstream out;
    out << "{\"schemaVersion\":1,\"case\":" << caseJson(*spec)
        << ",\"suite\":" << quote(sub::name(*suite))
        << ",\"lane\":" << quote(sub::name(options.lane)) << ",\"pair\":" << quote(options.pair)
        << ",\"order\":" << quote(options.order) << ",\"warmup\":" << options.config.warmup
        << ",\"frameCount\":" << options.config.frames
        << ",\"manifestHash\":" << quote(hash(manifest)) << ','
        << identities(options.config.shaderDirectory) << ",\"environment\":" << env
        << ",\"environmentHash\":" << quote(hash(env)) << ",\"runs\":[";
    const std::array modes = options.order == "AB" ? std::array{*a, *b} : std::array{*b, *a};
    for (size_t i = 0; i < modes.size(); ++i) {
        auto run = sub::runNative(*spec, *suite, modes[i], options.lane, options.config);
        if (!run) {
            write(options.output / "failure.json", "{\"error\":" + quote(run.error()) + "}");
            throw std::runtime_error(run.error());
        }
        if (i)
            out << ',';
        out << "{\"variant\":" << quote(sub::name(modes[i])) << ",\"frames\":" << samplesJson(*run)
            << ",\"throughput\":" << number(run->throughput)
            << ",\"setupMs\":" << number(run->setupMs) << ",\"drainMs\":" << number(run->drainMs)
            << ",\"requestedBytes\":" << run->requestedBytes
            << ",\"allocatedBytes\":" << run->allocatedBytes
            << ",\"residentBytes\":null,\"device\":" << quote(run->device)
            << ",\"gpuSpanStatus\":" << quote(run->gpuSpanStatus) << '}';
    }
    out << "]}";
    write(options.output / "result.json", out.str());
}

//======================================================================================================================
void diagnose(const Options& options) {
    if (options.caseId == "all" || options.suite == "all" || options.variant == "all")
        throw std::runtime_error("diagnosis requires one explicit case, suite and variant");
    if (options.lane != sub::Lane::Headline || !options.config.capturePath.empty() ||
        enabled("MTL_CAPTURE_ENABLED"))
        throw std::runtime_error("diagnosis refuses captures and timestamp lanes");
    if (options.config.frames > 900 || options.config.warmup > 32)
        throw std::runtime_error("diagnosis is bounded to 900 frames and 32 warmup frames");
    auto spec = sub::findCase(options.caseId);
    auto suite = sub::parseSuite(options.suite);
    auto variant = sub::parseVariant(options.variant);
    if (!spec || !suite || !variant)
        throw std::runtime_error("invalid diagnostic case, suite or variant");
    if (*variant == sub::Variant::GpuIcb)
        throw std::runtime_error("diagnosis requires an implemented ordinary variant");
    if (options.diagnosticDependency == "all" && *variant != sub::Variant::GpuArgs)
        throw std::runtime_error("all-stage diagnostic dependency requires gpu-args");
    prepareOutput(options.output);
    const auto manifest = sub::manifestJson(*spec);
    write(options.output / "manifest.json", manifest);
    const auto identity = identities(options.config.shaderDirectory);
    const std::string context =
        "{\"schemaVersion\":1,\"format\":\"lmx.submission.diagnostic\","
        "\"scored\":false,\"protocol\":\"pipelined-feedback-dependency-v2\",\"case\":" +
        caseJson(*spec) + ",\"suite\":" + quote(options.suite) +
        ",\"variant\":" + quote(options.variant) +
        ",\"warmup\":" + std::to_string(options.config.warmup) +
        ",\"frameCount\":" + std::to_string(options.config.frames) +
        ",\"manifestHash\":" + quote(hash(manifest)) + ',' + identity +
        ",\"environment\":" + environment() + ",\"diagnosticDependency\":" +
        quote(options.diagnosticDependency.empty() ? "declared" : options.diagnosticDependency);
    write(options.output / "diagnostic-start.json", context + '}');
    std::cerr << "UNSCORED diagnostic start case=" << options.caseId << " suite=" << options.suite
              << " mode=" << options.variant << '\n';
    auto config = options.config;
    config.diagnostics = true;
    config.verify = false;
    config.diagnosticAllStages = options.diagnosticDependency == "all";
    auto run = sub::runNative(*spec, *suite, *variant, sub::Lane::Headline, config);
    if (!run) {
        write(options.output / "diagnostic.json",
              context + ",\"status\":\"failed\",\"error\":" + quote(run.error()) + '}');
        throw std::runtime_error(run.error());
    }
    write(options.output / "diagnostic.json",
          context + ",\"status\":\"retired\",\"verified\":false,\"retiredFrames\":" +
              std::to_string(run->samples.size()) + '}');
    std::cerr << "UNSCORED diagnostic retired; no image/argument parity claim\n";
}

//======================================================================================================================
void compareImages(const sub::FrameImage& reference, const sub::FrameImage& candidate) {
    if (reference.visibleIds != candidate.visibleIds)
        throw std::runtime_error("visible ID disagreement");
    if (reference.rgba.size() != 4 * sub::kExtent * sub::kExtent ||
        reference.rgba.size() != candidate.rgba.size())
        throw std::runtime_error("invalid image dimensions");
    for (size_t i = 0; i < reference.rgba.size(); ++i) {
        // Clear is black and every material has positive RGB; coverage is independent of tolerance.
        if (i % 4 == 0) {
            const bool coveredReference =
                reference.rgba[i] || reference.rgba[i + 1] || reference.rgba[i + 2];
            const bool coveredCandidate =
                candidate.rgba[i] || candidate.rgba[i + 1] || candidate.rgba[i + 2];
            if (coveredReference != coveredCandidate)
                throw std::runtime_error("coverage parity failed at pixel " +
                                         std::to_string(i / 4));
        }
        if (std::abs(int(reference.rgba[i]) - int(candidate.rgba[i])) > 1)
            throw std::runtime_error("image parity failed at channel " + std::to_string(i));
    }
}

//======================================================================================================================
void verify(const Options& options) {
    if (!options.config.capturePath.empty() &&
        (options.caseId == "all" || options.suite == "all" || options.variant == "all"))
        throw std::runtime_error("capture requires one explicit case, suite and variant");
    if (!options.config.capturePath.empty() && fs::exists(options.config.capturePath))
        throw std::runtime_error("capture path already exists");
    prepareOutput(options.output);
    auto cases = sub::caseMatrix();
    if (options.caseId != "all") {
        auto spec = sub::findCase(options.caseId);
        if (!spec)
            throw std::runtime_error(spec.error());
        cases = {*spec};
    }
    std::vector<sub::Suite> suites{sub::Suite::S, sub::Suite::E};
    if (options.suite != "all") {
        auto suite = sub::parseSuite(options.suite);
        if (!suite)
            throw std::runtime_error(suite.error());
        suites = {*suite};
    }
    std::vector<sub::Variant> modes{sub::Variant::Direct, sub::Variant::CpuIndirect,
                                    sub::Variant::GpuArgs, sub::Variant::Batched};
    if (options.variant != "all") {
        auto mode = sub::parseVariant(options.variant);
        if (!mode)
            throw std::runtime_error(mode.error());
        modes = {*mode};
    }
    auto config = options.config;
    config.verify = true;
    auto replayConfig = config;
    replayConfig.capturePath.clear();
    std::ostringstream rows;
    rows << '[';
    bool first = true;
    for (const auto& spec : cases) {
        write(options.output / (spec.id + "-manifest.json"), sub::manifestJson(spec));
        for (auto suite : suites) {
            std::vector<sub::FrameImage> references;
            for (uint32_t phase = 0; phase < 8; ++phase) {
                auto reference = sub::renderReferenceFrame(spec, suite, sub::Variant::Direct,
                                                           phase * 32, config.shaderDirectory);
                if (!reference)
                    throw std::runtime_error(reference.error());
                references.push_back(std::move(*reference));
            }
            for (auto mode : modes) {
                std::cerr << "verify " << spec.id << ' ' << sub::name(suite) << ' '
                          << sub::name(mode) << '\n';
                for (uint32_t phase = 0; phase < 8; ++phase) {
                    std::cerr << "  scored-artifact phase " << phase << '\n';
                    auto image =
                        sub::renderNativeFrame(spec, suite, mode, phase * 32, replayConfig);
                    if (!image)
                        throw std::runtime_error(image.error());
                    compareImages(references[phase], *image);
                    if (mode != sub::Variant::Direct && mode != sub::Variant::GpuIcb) {
                        auto reference = sub::renderReferenceFrame(spec, suite, mode, phase * 32,
                                                                   config.shaderDirectory);
                        if (!reference)
                            throw std::runtime_error(reference.error());
                        compareImages(*reference, *image);
                        if (!phase)
                            write(options.output / (spec.id + "-" + sub::name(suite) + "-" +
                                                    sub::name(mode) + ".txt"),
                                  reference->graphDump);
                    }
                }
                std::cerr << "  retired replay " << config.frames << " frames\n";
                auto run = sub::runNative(spec, suite, mode, sub::Lane::Headline, config);
                if (!run || !run->verified)
                    throw std::runtime_error(run ? "run did not verify" : run.error());
                if (!first)
                    rows << ',';
                first = false;
                rows << "{\"case\":" << quote(spec.id) << ",\"suite\":" << quote(sub::name(suite))
                     << ",\"variant\":" << quote(sub::name(mode)) << ",\"frames\":" << config.frames
                     << ",\"manifestHash\":" << quote(hash(sub::manifestJson(spec)))
                     << ",\"reference\":true,\"scoredReplay\":true,\"retirement\":true}";
                write(options.output / "progress.json", rows.str() + ']');
            }
        }
    }
    rows << ']';
    write(options.output / "validation.json",
          "{\"schemaVersion\":1," + identities(config.shaderDirectory) +
              ",\"environment\":" + environment() + ",\"results\":" + rows.str() + '}');
}

//======================================================================================================================
int execute(const Options& options) {
    if (options.action == "--list-cases") {
        std::cout << '[';
        auto cases = sub::caseMatrix();
        for (size_t i = 0; i < cases.size(); ++i)
            std::cout << (i ? "," : "") << caseJson(cases[i]);
        std::cout << "]\n";
    } else if (options.action == "--capabilities") {
        auto data = sub::capabilitiesJson();
        if (!options.output.empty()) {
            prepareOutput(options.output);
            write(options.output / "capabilities.json", data);
        }
        std::cout << data << '\n';
    } else if (options.action == "--selftest") {
        if (hash("abc") != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
            throw std::runtime_error("SHA256 selftest failed");
        if (sub::caseMatrix().size() != 20)
            throw std::runtime_error("invalid frozen matrix");
        for (const auto& spec : sub::caseMatrix()) {
            auto frame = sub::makeFrame(spec, 0);
            if (!frame || sub::classify(*frame) != frame->visibleIds)
                throw std::runtime_error("model selftest failed");
        }
        std::cout << "selftest passed\n";
    } else if (options.action == "--measure")
        measure(options);
    else if (options.action == "--diagnose")
        diagnose(options);
    else
        verify(options);
    return 0;
}
} // namespace

//======================================================================================================================
int main(int argc, char** argv) {
    try {
        return execute(parse(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << "GpuSubmissionBench: " << error.what() << '\n';
        return 1;
    }
}
