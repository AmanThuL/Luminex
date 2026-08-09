#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <string>

//======================================================================================================================
// The Python tool suite, run by the same `xmake test` gate as everything else. Shelling out is
// deliberate: the tools are stdlib-only Python and this is the boring way to keep one test
// command. LMX_REPO_ROOT comes from xmake.lua because the Tests binary runs with CWD set to its
// own target dir, not the repo.
TEST_CASE("GpuDebug Python tool tests pass", "[tools]") {
    if (std::system("python3 --version > /dev/null 2>&1") != 0) {
        SKIP("python3 not available");
    }
    const std::string cmd = std::string("python3 -m unittest discover -s \"") + LMX_REPO_ROOT +
                            "/Tools/GpuDebug/tests\" 2>&1";
    REQUIRE(std::system(cmd.c_str()) == 0);
}
