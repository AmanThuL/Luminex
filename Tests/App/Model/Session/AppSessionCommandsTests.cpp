#include "App/Model/Session/SessionCommands.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

using namespace lmx::app;

//======================================================================================================================
TEST_CASE("session command table matches the protocol record and tiers",
          "[app][session-commands]") {
    constexpr std::array<std::string_view, 11> read = {
        "query.status",   "query.hierarchy", "query.selection",   "query.camera",
        "query.settings", "query.readings",  "query.performance", "query.graph",
        "query.console",  "query.proposals", "query.log"};
    constexpr std::array<std::string_view, 2> propose = {"propose.edits", "propose.withdraw"};
    constexpr std::array<std::string_view, 9> apply = {
        "settings.set",       "debugview.set",    "scene.open", "measure.run", "capture.gpu",
        "capture.screenshot", "capture.sequence", "graph.dump", "plan.submit"};
    const auto specs = sessionCommands();
    REQUIRE(specs.size() == 23);
    std::unordered_set<std::string_view> names;
    for (const auto& spec : specs) {
        CHECK(names.insert(spec.name).second);
        REQUIRE(findCommand(spec.name));
        CHECK(findCommand(spec.name)->command == spec.command);
    }
    CHECK(findCommand("hello") != nullptr);
    CHECK(findCommand("hello")->tier == SessionTier::ReadOnly);
    for (const auto name : read) {
        REQUIRE(findCommand(name));
        CHECK(findCommand(name)->tier == SessionTier::ReadOnly);
    }
    for (const auto name : propose) {
        REQUIRE(findCommand(name));
        CHECK(findCommand(name)->tier == SessionTier::Propose);
    }
    for (const auto name : apply) {
        REQUIRE(findCommand(name));
        CHECK(findCommand(name)->tier == SessionTier::Apply);
    }
    CHECK(findCommand("query.unknown") == nullptr);
}

//======================================================================================================================
TEST_CASE("each session command obeys all three permission ceilings", "[app][session-commands]") {
    for (const auto& spec : sessionCommands()) {
        if (spec.command == SessionCommand::Hello)
            continue;
        for (const auto ceiling :
             {SessionTier::ReadOnly, SessionTier::Propose, SessionTier::Apply}) {
            const auto refusal = tierRefusal(spec, ceiling);
            CHECK(refusal.has_value() == (spec.tier > ceiling));
            if (refusal)
                CHECK(refusal->find(spec.name) != std::string::npos);
        }
    }
}

//======================================================================================================================
TEST_CASE("evidence names are confined to safe single path components", "[app][session-commands]") {
    const std::array<std::string, 3> acceptedNames = {"frame_01.png", "A-Z.9",
                                                      std::string(128, 'x')};
    for (const auto& name : acceptedNames) {
        const auto accepted = evidenceName(name);
        REQUIRE(accepted);
        CHECK(*accepted == name);
    }
    const std::array<std::string, 9> rejectedNames = {
        "", ".hidden", "a..b", "..", "a/b", "a\\b", "a b", "é", std::string(129, 'x')};
    for (const auto& name : rejectedNames) {
        INFO(name);
        CHECK_FALSE(evidenceName(name));
    }
}

//======================================================================================================================
TEST_CASE("child run environment leaves out every editor diagnostic variable", "[app][session]") {
    const std::array<const char*, 8> environment{"PATH=/usr/bin",
                                                 "LMX_GRAPH_DUMP=/tmp/graph.txt",
                                                 "HOME=/var/empty",
                                                 "LMX_MAX_FRAMES=4",
                                                 "LMX_CAPTURE_PATH=/tmp/out.gputrace",
                                                 "MTL_DEBUG_LAYER=1",
                                                 "NOTE=LMX_INSIDE=1",
                                                 nullptr};
    const auto kept = childRunEnvironment(environment.data());
    CHECK(kept == std::vector<std::string>{"PATH=/usr/bin", "HOME=/var/empty", "MTL_DEBUG_LAYER=1",
                                           "NOTE=LMX_INSIDE=1"});
    CHECK(childRunEnvironment(nullptr).empty());
}
