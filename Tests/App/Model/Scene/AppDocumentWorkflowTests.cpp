#include "App/Model/Scene/DocumentWorkflow.h"
#include <catch2/catch_test_macros.hpp>

using namespace lmx::app;

//======================================================================================================================
TEST_CASE("document workflow gates every clean and dirty action", "[app][document-workflow]") {
    for (const bool dirty : {false, true}) {
        for (const auto action :
             {DocumentAction::Open, DocumentAction::OpenCatalog, DocumentAction::Save,
              DocumentAction::SaveAs, DocumentAction::Revert, DocumentAction::Quit}) {
            DocumentWorkflow flow;
            flow.setContext(dirty, true, false);
            REQUIRE(flow.request(action, lmx::scenes::SceneId{"material-lab"}));
            const bool destructive =
                action != DocumentAction::Save && action != DocumentAction::SaveAs;
            if (dirty && destructive) {
                REQUIRE(flow.step() == WorkflowStep::Confirm);
                REQUIRE_FALSE(flow.takeWork());
                flow.confirm(ConfirmChoice::Discard);
                REQUIRE(flow.dirty());
            }
            if (action == DocumentAction::Open || action == DocumentAction::SaveAs) {
                REQUIRE(flow.step() == WorkflowStep::ChoosePath);
                flow.pathChosen("/tmp/chosen.scene.gltf");
            }
            REQUIRE(flow.step() == WorkflowStep::Ready);
            auto work = flow.takeWork();
            REQUIRE(work);
            REQUIRE(work->action == action);
            REQUIRE_FALSE(work->saveFirst);
            REQUIRE_FALSE(flow.takeWork());
            flow.complete(true);
            REQUIRE(flow.step() == WorkflowStep::Idle);
        }
    }
}

//======================================================================================================================
TEST_CASE("document workflow cancellation never exposes destructive work",
          "[app][document-workflow]") {
    DocumentWorkflow flow;
    flow.setContext(true, true, false);
    REQUIRE(flow.request(DocumentAction::Open));
    flow.confirm(ConfirmChoice::Cancel);
    REQUIRE(flow.step() == WorkflowStep::Idle);
    REQUIRE(flow.dirty());
    REQUIRE_FALSE(flow.takeWork());
    REQUIRE(flow.request(DocumentAction::Open));
    flow.confirm(ConfirmChoice::Save);
    REQUIRE(flow.step() == WorkflowStep::Ready);
    const auto save = flow.takeWork();
    REQUIRE(save);
    REQUIRE(save->action == DocumentAction::Save);
    REQUIRE_FALSE(save->saveFirst);
    REQUIRE_FALSE(save->path);
    flow.complete(true);
    REQUIRE(flow.step() == WorkflowStep::ChoosePath);
    REQUIRE_FALSE(flow.dirty());
    flow.pathChosen({});
    REQUIRE_FALSE(flow.takeWork());
    REQUIRE(flow.step() == WorkflowStep::Idle);
    REQUIRE_FALSE(flow.dirty());
    flow.setContext(true, true, false);
    REQUIRE(flow.request(DocumentAction::SaveAs));
    flow.pathChosen({});
    REQUIRE_FALSE(flow.takeWork());
    REQUIRE(flow.dirty());
}

//======================================================================================================================
TEST_CASE("failed save-first aborts switch and preserves dirty state", "[app][document-workflow]") {
    for (const auto action : {DocumentAction::OpenCatalog, DocumentAction::Quit}) {
        DocumentWorkflow flow;
        flow.setContext(true, true, false);
        REQUIRE(flow.request(action, lmx::scenes::SceneId{"sponza"}));
        flow.confirm(ConfirmChoice::Save);
        auto work = flow.takeWork();
        REQUIRE(work);
        REQUIRE(work->saveFirst);
        REQUIRE(work->action == action);
        flow.complete(false);
        REQUIRE(flow.dirty());
        REQUIRE(flow.step() == WorkflowStep::Idle);
        REQUIRE_FALSE(flow.takeWork());
        REQUIRE(flow.request(action));
        REQUIRE(flow.step() == WorkflowStep::Confirm);
    }
}

//======================================================================================================================
TEST_CASE("queued quit preserves a selected native path until its operation completes",
          "[app][document-workflow][document-repair]") {
    for (const auto action : {DocumentAction::Open, DocumentAction::SaveAs}) {
        for (const bool dirty : {false, true}) {
            for (const bool succeeded : {false, true}) {
                DocumentWorkflow flow;
                flow.setContext(dirty, true, false);
                REQUIRE(flow.request(action));
                if (flow.step() == WorkflowStep::Confirm)
                    flow.confirm(ConfirmChoice::Discard);
                REQUIRE(flow.step() == WorkflowStep::ChoosePath);
                REQUIRE(flow.request(DocumentAction::Quit));
                REQUIRE(flow.request(DocumentAction::Quit));
                REQUIRE_FALSE(flow.takeWork());
                const std::filesystem::path selected = "/tmp/selected.scene.gltf";
                flow.pathChosen(selected);
                auto work = flow.takeWork();
                REQUIRE(work);
                REQUIRE(work->action == action);
                REQUIRE(work->path == selected);
                REQUIRE_FALSE(flow.takeWork());
                flow.setContext(succeeded ? false : dirty, true, false);
                flow.complete(succeeded);
                if (!succeeded && dirty) {
                    REQUIRE(flow.step() == WorkflowStep::Confirm);
                    flow.confirm(ConfirmChoice::Cancel);
                    REQUIRE_FALSE(flow.takeWork());
                    REQUIRE(flow.dirty());
                } else {
                    auto quit = flow.takeWork();
                    REQUIRE(quit);
                    REQUIRE(quit->action == DocumentAction::Quit);
                    REQUIRE_FALSE(flow.takeWork());
                }
            }
        }
    }
}

//======================================================================================================================
TEST_CASE("queued quit follows explicit native cancellation without selected work",
          "[app][document-workflow][document-repair]") {
    for (const auto action : {DocumentAction::Open, DocumentAction::SaveAs}) {
        for (const bool dirty : {false, true}) {
            DocumentWorkflow flow;
            flow.setContext(dirty, true, false);
            REQUIRE(flow.request(action));
            if (flow.step() == WorkflowStep::Confirm)
                flow.confirm(ConfirmChoice::Discard);
            REQUIRE(flow.request(DocumentAction::Quit));
            flow.pathChosen({});
            if (dirty) {
                REQUIRE(flow.step() == WorkflowStep::Confirm);
                flow.confirm(ConfirmChoice::Cancel);
                REQUIRE_FALSE(flow.takeWork());
                REQUIRE(flow.dirty());
            } else {
                REQUIRE(flow.takeWork()->action == DocumentAction::Quit);
            }
        }
    }
}

//======================================================================================================================
TEST_CASE("document mutations and save choice refuse preview and measurement",
          "[app][document-workflow]") {
    for (const bool measuring : {false, true}) {
        for (const bool stopped : {false, true}) {
            for (const auto action :
                 {DocumentAction::Save, DocumentAction::SaveAs, DocumentAction::Revert}) {
                DocumentWorkflow flow;
                flow.setContext(true, stopped, measuring);
                REQUIRE(
                    DocumentWorkflow::unavailableReason(action, stopped, measuring).has_value() ==
                    (!stopped || measuring));
                REQUIRE(flow.request(action) == (stopped && !measuring));
            }
            DocumentWorkflow flow;
            flow.setContext(true, stopped, measuring);
            REQUIRE(flow.request(DocumentAction::Quit));
            flow.confirm(ConfirmChoice::Save);
            if (!stopped || measuring) {
                REQUIRE(flow.step() == WorkflowStep::Confirm);
                REQUIRE_FALSE(flow.takeWork());
                flow.confirm(ConfirmChoice::Discard);
                REQUIRE(flow.takeWork()->action == DocumentAction::Quit);
            } else {
                REQUIRE(flow.takeWork()->saveFirst);
            }
        }
    }
}

//======================================================================================================================
TEST_CASE("pending file proposal guards both save paths until review", "[app][document-workflow]") {
    DocumentWorkflow flow;
    flow.setContext(true, true, false, true);
    for (const auto action : {DocumentAction::Save, DocumentAction::SaveAs}) {
        CHECK(DocumentWorkflow::unavailableReason(action, true, false, true) ==
              "Review the pending proposal first");
        CHECK_FALSE(flow.request(action));
    }
    CHECK_FALSE(DocumentWorkflow::unavailableReason(DocumentAction::Revert, true, false, true));
    flow.setContext(true, true, false, false);
    CHECK(flow.request(DocumentAction::Save));
}

//======================================================================================================================
TEST_CASE("busy document requests cannot replace a pending operation", "[app][document-workflow]") {
    DocumentWorkflow flow;
    flow.setContext(false, true, false);
    REQUIRE(flow.request(DocumentAction::SaveAs));
    REQUIRE_FALSE(flow.request(DocumentAction::OpenCatalog));
    flow.pathChosen("copy.scene.gltf");
    REQUIRE(flow.takeWork()->action == DocumentAction::SaveAs);
    REQUIRE_FALSE(flow.request(DocumentAction::Save));
    flow.complete(false);
    REQUIRE(flow.step() == WorkflowStep::Idle);
}

//======================================================================================================================
TEST_CASE("repeated quit preserves a completed discard choice", "[app][document-workflow]") {
    DocumentWorkflow flow;
    flow.setContext(true, true, false);
    REQUIRE(flow.request(DocumentAction::Quit));
    flow.confirm(ConfirmChoice::Discard);
    REQUIRE(flow.request(DocumentAction::Quit));
    CHECK(flow.step() == WorkflowStep::Ready);
    REQUIRE(flow.takeWork()->action == DocumentAction::Quit);
    REQUIRE(flow.request(DocumentAction::Quit));
    CHECK_FALSE(flow.takeWork());
}

//======================================================================================================================
TEST_CASE("quit queued behind a save waits for the save result", "[app][document-workflow]") {
    for (const bool succeeded : {false, true}) {
        DocumentWorkflow flow;
        flow.setContext(true, true, false);
        REQUIRE(flow.request(DocumentAction::Save));
        REQUIRE(flow.takeWork());
        REQUIRE(flow.request(DocumentAction::Quit));
        REQUIRE_FALSE(flow.takeWork());
        flow.complete(succeeded);
        if (succeeded) {
            REQUIRE_FALSE(flow.dirty());
            REQUIRE(flow.takeWork()->action == DocumentAction::Quit);
        } else {
            REQUIRE(flow.dirty());
            REQUIRE(flow.step() == WorkflowStep::Confirm);
            flow.confirm(ConfirmChoice::Cancel);
            REQUIRE_FALSE(flow.takeWork());
        }
    }
}

//======================================================================================================================
TEST_CASE("Open confirmation saves before choosing a path and retains queued Quit",
          "[app][document-workflow][save-before-open]") {
    for (bool succeeds : {false, true}) {
        for (bool selected : {false, true}) {
            DocumentWorkflow flow;
            flow.setContext(true, true, false);
            REQUIRE(flow.request(DocumentAction::Open));
            flow.confirm(ConfirmChoice::Save);
            REQUIRE(flow.step() == WorkflowStep::Ready);
            const auto save = flow.takeWork();
            REQUIRE(save);
            CHECK(save->action == DocumentAction::Save);
            CHECK_FALSE(save->saveFirst);
            CHECK_FALSE(save->path);
            REQUIRE(flow.request(DocumentAction::Quit));
            REQUIRE_FALSE(flow.takeWork());
            flow.complete(succeeds);
            if (!succeeds) {
                CHECK(flow.step() == WorkflowStep::Confirm);
                CHECK(flow.action() == DocumentAction::Quit);
                CHECK(flow.dirty());
                CHECK_FALSE(flow.takeWork());
                continue;
            }
            REQUIRE(flow.step() == WorkflowStep::ChoosePath);
            CHECK(flow.action() == DocumentAction::Open);
            CHECK_FALSE(flow.dirty());
            CHECK_FALSE(flow.takeWork());
            flow.pathChosen(selected ? std::optional(std::filesystem::path("chosen.scene.gltf"))
                                     : std::nullopt);
            if (selected) {
                const auto open = flow.takeWork();
                REQUIRE(open);
                CHECK(open->action == DocumentAction::Open);
                CHECK(open->path == std::filesystem::path("chosen.scene.gltf"));
                CHECK_FALSE(open->saveFirst);
                CHECK_FALSE(flow.takeWork());
                flow.complete(true);
            }
            const auto quit = flow.takeWork();
            REQUIRE(quit);
            CHECK(quit->action == DocumentAction::Quit);
            CHECK_FALSE(flow.takeWork());
        }
    }
}

//======================================================================================================================
TEST_CASE("Quit cannot replace an accepted Open save before its work is taken",
          "[app][document-workflow][save-before-open]") {
    DocumentWorkflow flow;
    flow.setContext(true, true, false);
    REQUIRE(flow.request(DocumentAction::Open));
    flow.confirm(ConfirmChoice::Save);
    REQUIRE(flow.request(DocumentAction::Quit));
    REQUIRE(flow.request(DocumentAction::Quit));
    const auto save = flow.takeWork();
    REQUIRE(save);
    CHECK(save->action == DocumentAction::Save);
    CHECK_FALSE(flow.takeWork());
    flow.complete(true);
    REQUIRE(flow.step() == WorkflowStep::ChoosePath);
    CHECK(flow.action() == DocumentAction::Open);
    flow.pathChosen({});
    const auto quit = flow.takeWork();
    REQUIRE(quit);
    CHECK(quit->action == DocumentAction::Quit);
}

//======================================================================================================================
TEST_CASE("Quit queues behind Ready Save that was not yet taken", "[app][document-workflow]") {
    DocumentWorkflow flow;
    flow.setContext(true, true, false);
    REQUIRE(flow.request(DocumentAction::Save));
    REQUIRE(flow.step() == WorkflowStep::Ready);
    REQUIRE(flow.request(DocumentAction::Quit));
    const auto save = flow.takeWork();
    REQUIRE(save);
    CHECK(save->action == DocumentAction::Save);
    flow.complete(true);
    const auto quit = flow.takeWork();
    REQUIRE(quit);
    CHECK(quit->action == DocumentAction::Quit);
}

//======================================================================================================================
TEST_CASE("Quit queues behind Ready Revert that was not yet taken", "[app][document-workflow]") {
    DocumentWorkflow flow;
    flow.setContext(true, true, false);
    REQUIRE(flow.request(DocumentAction::Revert));
    flow.confirm(ConfirmChoice::Discard);
    REQUIRE(flow.step() == WorkflowStep::Ready);
    REQUIRE(flow.request(DocumentAction::Quit));
    CHECK(flow.takeWork()->action == DocumentAction::Revert);
}

//======================================================================================================================
TEST_CASE("Revert confirmation never offers or accepts Save", "[app][document-workflow]") {
    CHECK_FALSE(DocumentWorkflow::offersSave(DocumentAction::Revert));
    CHECK(DocumentWorkflow::offersSave(DocumentAction::Open));
    CHECK(DocumentWorkflow::offersSave(DocumentAction::Quit));
    DocumentWorkflow flow;
    flow.setContext(true, true, false);
    REQUIRE(flow.request(DocumentAction::Revert));
    REQUIRE(flow.step() == WorkflowStep::Confirm);
    flow.confirm(ConfirmChoice::Save);
    CHECK(flow.step() == WorkflowStep::Confirm);
}
