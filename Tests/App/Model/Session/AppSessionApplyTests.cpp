#include "App/Model/Session/SessionApply.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>

using namespace lmx::app;

namespace {
struct ScopedDirectory {
    std::filesystem::path path;
    //==================================================================================================================
    ScopedDirectory() {
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        path =
            std::filesystem::temp_directory_path() / ("lmx-apply-capture-" + std::to_string(tick));
        if (!std::filesystem::create_directory(path))
            path.clear();
    }
    //==================================================================================================================
    ~ScopedDirectory() {
        if (!path.empty())
            std::filesystem::remove_all(path);
    }
};

//======================================================================================================================
lmx::asset::JsonNode args(std::string text) {
    return lmx::asset::JsonTokens::parse(std::move(text))->root();
}
} // namespace

//======================================================================================================================
TEST_CASE("apply requests freeze validated steps before approval", "[app][session-apply]") {
    const auto single =
        parseApplyRequest(SessionCommand::SettingsSet, args(R"({"temporal":"off"})"));
    REQUIRE(single);
    REQUIRE(single->steps.size() == 1);
    CHECK(single->steps[0].command == SessionCommand::SettingsSet);
    CHECK(single->steps[0].arguments == R"({"temporal":"off"})");

    const auto plan = parseApplyRequest(
        SessionCommand::PlanSubmit,
        args(
            R"({"summary":"Evidence","steps":[{"command":"settings.set","args":{"temporal":"off"}},{"command":"graph.dump","args":{"name":"frame.txt"}}]})"));
    REQUIRE(plan);
    REQUIRE(plan->steps.size() == 2);
    CHECK(plan->steps[1].command == SessionCommand::GraphDump);
    CHECK_FALSE(
        parseApplyRequest(SessionCommand::PlanSubmit, args(R"({"summary":"x","steps":[]})")));
    CHECK_FALSE(parseApplyRequest(
        SessionCommand::PlanSubmit,
        args(R"({"summary":"x","steps":[{"command":"plan.submit","args":{}}]})")));
}

//======================================================================================================================
TEST_CASE("apply arguments reject unsafe outputs and malformed work", "[app][session-apply]") {
    CHECK_FALSE(parseApplyRequest(SessionCommand::GraphDump, args(R"({"name":"../escape"})")));
    CHECK_FALSE(parseApplyRequest(SessionCommand::MeasureRun,
                                  args(R"({"name":"m.json","warmup":0,"frames":0})")));
    CHECK_FALSE(parseApplyRequest(SessionCommand::SceneOpen, args(R"({"scene":""})")));
    CHECK_FALSE(parseApplyRequest(SessionCommand::DebugViewSet,
                                  args(R"({"topic":"temporal","value":"bad"})")));
    CHECK_FALSE(parseApplyRequest(SessionCommand::SettingsSet, args(R"({"temporal":"bogus"})")));
    CHECK_FALSE(parseApplyRequest(SessionCommand::SettingsSet, args(R"({"render-scale":"2"})")));
    CHECK_FALSE(parseApplyRequest(
        SessionCommand::PlanSubmit,
        args(
            R"({"summary":"bad","steps":[{"command":"settings.set","args":{"temporal":"off"}},{"command":"settings.set","args":{"render-scale":"2"}}]})")));
    CHECK_FALSE(parseApplyRequest(SessionCommand::MeasureRun,
                                  args(R"({"name":"m","warmup":4294967295,"frames":1})")));
    CHECK(parseApplyRequest(SessionCommand::CaptureScreenshot,
                            args(R"({"name":"still","frames":1})")));
}

//======================================================================================================================
TEST_CASE("session output directory name is deterministic UTC", "[app][session-apply]") {
    CHECK(sessionDirectoryName(0, 42) == "session/19700101-000000-42");
    CHECK(sessionDirectoryName(1'760'000'000, 99).starts_with("session/20251009-"));
    CHECK(captureGpuOutputName(12, 0) == "capture-12-1.gputrace");
    CHECK(captureGpuOutputName(12, 1) == "capture-12-2.gputrace");
}

//======================================================================================================================
TEST_CASE("GPU capture refuses existing trace and schema companion paths", "[app][session-apply]") {
    ScopedDirectory directory;
    REQUIRE_FALSE(directory.path.empty());
    const auto trace = directory.path / "capture.gputrace";
    const std::array paths{trace, std::filesystem::path(trace.string() + ".schema.json"),
                           std::filesystem::path(trace.string() + ".schema.json.tmp")};
    REQUIRE(sessionCapturePathsAvailable(trace));
    for (const auto& path : paths) {
        {
            std::ofstream file(path);
            file << "owner";
        }
        CHECK_FALSE(sessionCapturePathsAvailable(trace));
        std::ifstream file(path);
        std::string contents;
        file >> contents;
        CHECK(contents == "owner");
        file.close();
        std::filesystem::remove(path);
        REQUIRE(sessionCapturePathsAvailable(trace));

        std::filesystem::create_symlink(directory.path / "missing", path);
        CHECK_FALSE(sessionCapturePathsAvailable(trace));
        CHECK(std::filesystem::is_symlink(path));
        std::filesystem::remove(path);
        REQUIRE(sessionCapturePathsAvailable(trace));
    }
}

//======================================================================================================================
TEST_CASE("scene open preserves only stable semantic selection", "[app][session-apply]") {
    CHECK(sessionSceneOpenAllowed(EditorSubject::None, false, true, true, false));
    CHECK(sessionSceneOpenAllowed(EditorSubject::Camera, false, true, true, false));
    CHECK(sessionSceneOpenAllowed(EditorSubject::Environment, false, true, true, false));
    CHECK_FALSE(sessionSceneOpenAllowed(EditorSubject::Object, false, true, true, false));
    CHECK_FALSE(sessionSceneOpenAllowed(EditorSubject::Camera, true, true, true, false));
    CHECK_FALSE(sessionSceneOpenAllowed(EditorSubject::Camera, false, false, true, false));
    CHECK_FALSE(sessionSceneOpenAllowed(EditorSubject::Camera, false, true, false, false));
    CHECK_FALSE(sessionSceneOpenAllowed(EditorSubject::Camera, false, true, true, true));
}

//======================================================================================================================
TEST_CASE("session record name is reserved before approval", "[app][session-apply]") {
    for (const auto command :
         {SessionCommand::GraphDump, SessionCommand::MeasureRun, SessionCommand::CaptureSequence}) {
        CHECK_FALSE(
            parseApplyRequest(command, args(R"({"name":"session.json","warmup":0,"frames":1})")));
    }
    CHECK_FALSE(parseApplyRequest(SessionCommand::GraphDump, args(R"({"name":"SESSION.JSON"})")));
    CHECK_FALSE(parseApplyRequest(
        SessionCommand::PlanSubmit,
        args(
            R"({"summary":"collision","steps":[{"command":"graph.dump","args":{"name":"session.json"}}]})")));
}

//======================================================================================================================
TEST_CASE("apply commands accept only their own argument names", "[app][session-apply]") {
    CHECK(parseApplyRequest(SessionCommand::SceneOpen, args(R"({"scene":"sponza"})")));
    CHECK_FALSE(
        parseApplyRequest(SessionCommand::SceneOpen, args(R"({"scene":"sponza","force":true})")));
    CHECK_FALSE(parseApplyRequest(SessionCommand::SceneOpen,
                                  args(R"({"scene":"sponza","scene":"light-lab"})")));
    CHECK(parseApplyRequest(SessionCommand::MeasureRun,
                            args(R"({"name":"m.json","warmup":0,"frames":4})")));
    CHECK_FALSE(parseApplyRequest(SessionCommand::MeasureRun,
                                  args(R"({"name":"m.json","warmup":0,"frames":4,"scene":"x"})")));
    CHECK_FALSE(parseApplyRequest(SessionCommand::MeasureRun,
                                  args(R"({"name":"m.json","warmup":0,"frames":4,"frames":9})")));
    CHECK_FALSE(
        parseApplyRequest(SessionCommand::GraphDump, args(R"({"name":"frame.txt","frames":1})")));
    CHECK_FALSE(parseApplyRequest(SessionCommand::CaptureScreenshot,
                                  args(R"({"name":"still","frames":1,"warmup":0})")));
    CHECK_FALSE(parseApplyRequest(SessionCommand::CaptureScreenshot,
                                  args(R"({"name":"still","name":"other","frames":1})")));
    CHECK(parseApplyRequest(SessionCommand::CaptureSequence,
                            args(R"({"name":"run","frames":2,"warmup":1})")));
    CHECK_FALSE(parseApplyRequest(SessionCommand::CaptureSequence,
                                  args(R"({"name":"run","frames":2,"warmup":1,"scale":"0.5"})")));
    CHECK_FALSE(parseApplyRequest(SessionCommand::DebugViewSet,
                                  args(R"({"topic":"temporal","topic":"lighting"})")));

    const auto unknown = parseApplyRequest(SessionCommand::SceneOpen, args(R"({"path":"x"})"));
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error() == "Unknown argument path for scene.open");
}

//======================================================================================================================
TEST_CASE("plans accept only known names at every level", "[app][session-apply]") {
    CHECK_FALSE(parseApplyRequest(
        SessionCommand::PlanSubmit,
        args(
            R"({"summary":"x","note":"y","steps":[{"command":"graph.dump","args":{"name":"a"}}]})")));
    CHECK_FALSE(parseApplyRequest(
        SessionCommand::PlanSubmit,
        args(
            R"({"summary":"x","summary":"y","steps":[{"command":"graph.dump","args":{"name":"a"}}]})")));
    CHECK_FALSE(parseApplyRequest(
        SessionCommand::PlanSubmit,
        args(
            R"({"summary":"x","steps":[{"command":"graph.dump","args":{"name":"a"},"tier":"apply"}]})")));
    CHECK_FALSE(parseApplyRequest(
        SessionCommand::PlanSubmit,
        args(
            R"({"summary":"x","steps":[{"command":"graph.dump","command":"capture.gpu","args":{"name":"a"}}]})")));
    CHECK_FALSE(parseApplyRequest(
        SessionCommand::PlanSubmit,
        args(
            R"({"summary":"x","steps":[{"command":"graph.dump","args":{"name":"a","frames":1}}]})")));
    const auto duplicate = parseApplyRequest(
        SessionCommand::PlanSubmit,
        args(
            R"({"summary":"x","steps":[{"command":"scene.open","args":{"scene":"a","scene":"b"}}]})"));
    REQUIRE_FALSE(duplicate);
    CHECK(duplicate.error() == "Duplicate argument scene");
}

//======================================================================================================================
TEST_CASE("approval cards name the output each step writes", "[app][session-apply]") {
    CHECK(
        sessionStepOutputName({SessionCommand::CaptureScreenshot, R"({"name":"still","frames":1})"},
                              4, 0) == "still.png");
    CHECK(sessionStepOutputName(
              {SessionCommand::CaptureSequence, R"({"name":"run","frames":2,"warmup":0})"}, 4, 0) ==
          "run");
    CHECK(sessionStepOutputName({SessionCommand::GraphDump, R"({"name":"frame.txt"})"}, 4, 0) ==
          "frame.txt");
    CHECK(sessionStepOutputName(
              {SessionCommand::MeasureRun, R"({"name":"m.json","warmup":0,"frames":1})"}, 4, 0) ==
          "m.json");
    CHECK(sessionStepOutputName({SessionCommand::CaptureGpu, "{}"}, 4, 2) ==
          "capture-4-3.gputrace");
    CHECK(sessionStepOutputName({SessionCommand::SettingsSet, R"({"temporal":"off"})"}, 4, 0)
              .empty());
    CHECK(sessionStepOutputName({SessionCommand::GraphDump, "not json"}, 4, 0).empty());
}

//======================================================================================================================
TEST_CASE("a running plan refuses the operator's scene replacement and nothing else",
          "[app][session-apply]") {
    constexpr std::array replacing{DocumentAction::Open, DocumentAction::OpenCatalog,
                                   DocumentAction::Revert};
    constexpr std::array keeping{DocumentAction::Save, DocumentAction::SaveAs,
                                 DocumentAction::Quit};
    for (const auto action : replacing) {
        const auto refusal = runningPlanRefusal(action, true);
        REQUIRE(refusal);
        CHECK(*refusal == "A session plan is running; press Stop in the toolbar before opening or "
                          "reverting a scene.");
        CHECK_FALSE(runningPlanRefusal(action, false));
    }
    for (const auto action : keeping) {
        CHECK_FALSE(runningPlanRefusal(action, true));
        CHECK_FALSE(runningPlanRefusal(action, false));
    }
}
