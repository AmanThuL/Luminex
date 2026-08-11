//----------------------------------------------------------------------------------------------------------------------
/// @file Runner.cpp
/// @brief Implements the adapter-neutral M5.1 bench runner.
//----------------------------------------------------------------------------------------------------------------------

#include "Bench/Runner.h"

#include <cstdio>
#include <fstream>
#include <iostream>

namespace lmx::noapi::bench {

//======================================================================================================================
uint64_t fnv1a64(std::span<const uint8_t> bytes) {
    constexpr uint64_t kOffsetBasis = 0xCBF29CE484222325ull;
    constexpr uint64_t kPrime = 0x100000001B3ull;
    uint64_t hash = kOffsetBasis;
    for (uint8_t byte : bytes) {
        hash ^= byte;
        hash *= kPrime;
    }
    return hash;
}

namespace {

//======================================================================================================================
bool writeDump(const std::filesystem::path& dumpDir, uint32_t frameIndex,
               std::span<const uint8_t> bytes) {
    std::error_code errorCode;
    std::filesystem::create_directories(dumpDir, errorCode);
    if (errorCode) {
        std::cerr << "runBench: cannot create dump directory '" << dumpDir.string()
                  << "': " << errorCode.message() << "\n";
        return false;
    }

    char name[32];
    std::snprintf(name, sizeof(name), "frame_%04u.bin", frameIndex);
    const std::filesystem::path path = dumpDir / name;

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        std::cerr << "runBench: cannot open '" << path.string() << "' for writing\n";
        return false;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}

} // namespace

//======================================================================================================================
int runBench(Adapter& adapter, const RunOptions& options) {
    if (!adapter.setup()) {
        std::cerr << "runBench: adapter setup failed\n";
        return 1;
    }

    std::vector<uint8_t> readback;
    bool ok = true;
    for (uint32_t frameIndex = 0; frameIndex < options.frames; ++frameIndex) {
        readback.clear();
        adapter.runFrame(frameIndex, readback);
        if (readback.empty()) {
            std::cerr << "runBench: frame " << frameIndex << " produced an empty readback\n";
            ok = false;
            break;
        }

        const uint64_t hash = fnv1a64(readback);
        std::cout << "frame " << frameIndex << " hash=" << std::hex << hash << std::dec << "\n";

        if (!writeDump(options.dumpDir, frameIndex, readback)) {
            ok = false;
            break;
        }
    }

    adapter.teardown();

    if (!ok) {
        return 1;
    }
    std::cout << "runBench: " << options.frames << " frames of graph '" << options.graph
              << "' completed, dumps written to " << options.dumpDir.string() << "\n";
    return 0;
}

} // namespace lmx::noapi::bench
