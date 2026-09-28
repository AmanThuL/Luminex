#include "App/Model/Scene/DocumentDialogMailbox.h"
#include "App/Model/Scene/DocumentWorkPump.h"
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <thread>

using namespace lmx::app;

//======================================================================================================================
TEST_CASE("native dialog mailbox queues worker results and cancellation exactly once",
          "[app][document-dialog]") {
    auto mailbox = std::make_shared<DocumentDialogMailbox>();
    REQUIRE(mailbox->begin());
    REQUIRE_FALSE(mailbox->begin());
    REQUIRE(mailbox->pending());
    REQUIRE_FALSE(mailbox->take());
    std::thread callback([retained = mailbox] { retained->post({.path = "picked.scene.gltf"}); });
    callback.join();
    REQUIRE(mailbox->pending());
    const auto result = mailbox->take();
    REQUIRE(result);
    REQUIRE(result->path == std::filesystem::path("picked.scene.gltf"));
    REQUIRE_FALSE(mailbox->pending());
    REQUIRE_FALSE(mailbox->take());
    REQUIRE(mailbox->begin());
    mailbox->post({});
    REQUIRE(mailbox->take());
    REQUIRE_FALSE(mailbox->pending());
}

//======================================================================================================================
TEST_CASE("a late dialog callback owns no shell pointer and outlives its owner",
          "[app][document-dialog]") {
    auto owner = std::make_shared<DocumentDialogMailbox>();
    const std::weak_ptr<DocumentDialogMailbox> lifetime = owner;
    REQUIRE(owner->begin());
    auto callbackOwner = owner;
    owner.reset();
    REQUIRE_FALSE(lifetime.expired());
    std::thread callback([retained = std::move(callbackOwner)] {
        retained->post({.error = "native dialog failed"});
    });
    callback.join();
    REQUIRE(lifetime.expired());
}

//======================================================================================================================
TEST_CASE("document pump progresses before unavailable drawables",
          "[app][document-dialog][document-repair]") {
    DocumentWorkflow workflow;
    DocumentDialogMailbox mailbox;
    int executions = 0;
    int acquisitions = 0;
    bool exited = false;
    bool dirty = false;
    SECTION("clean quit needs no drawable") {
        REQUIRE(workflow.request(DocumentAction::Quit));
    }
    SECTION("confirmed dirty quit needs no drawable") {
        workflow.setContext(true, true, false);
        REQUIRE(workflow.request(DocumentAction::Quit));
        workflow.confirm(ConfirmChoice::Discard);
    }
    SECTION("dirty confirmation waits without losing quit") {
        dirty = true;
        workflow.setContext(true, true, false);
        REQUIRE(workflow.request(DocumentAction::Quit));
    }
    const auto execute = [&](const PendingDocumentWork& work) {
        ++executions;
        CHECK(work.action == DocumentAction::Quit);
        return true;
    };
    // Match main's production boundary: pump, consume approved Quit, then acquire.
    for (int frame = 0; frame != 4; ++frame) {
        if (pumpDocumentWork(workflow, mailbox, execute, {})) {
            exited = true;
            break;
        }
        ++acquisitions;
        const bool drawableAvailable = false;
        if (!drawableAvailable)
            continue;
        FAIL("no UI frame is available");
    }
    if (dirty) {
        CHECK_FALSE(exited);
        CHECK(executions == 0);
        CHECK(acquisitions == 4);
        CHECK(workflow.step() == WorkflowStep::Confirm);
        CHECK(workflow.action() == DocumentAction::Quit);
        workflow.confirm(ConfirmChoice::Discard);
        CHECK(pumpDocumentWork(workflow, mailbox, execute, {}));
        CHECK(executions == 1);
    } else {
        CHECK(exited);
        CHECK(executions == 1);
        CHECK(acquisitions == 0);
    }
}

//======================================================================================================================
TEST_CASE("document pump consumes path cancellation and errors once before queued quit",
          "[app][document-dialog][document-repair]") {
    for (auto action : {DocumentAction::Open, DocumentAction::SaveAs}) {
        for (bool dirty : {false, true}) {
            for (int response = 0; response != 3; ++response) {
                for (bool succeeds : {false, true}) {
                    CAPTURE(action, dirty, response, succeeds);
                    DocumentWorkflow workflow;
                    DocumentDialogMailbox mailbox;
                    workflow.setContext(dirty, true, false);
                    REQUIRE(workflow.request(action));
                    if (workflow.step() == WorkflowStep::Confirm)
                        workflow.confirm(ConfirmChoice::Discard);
                    REQUIRE(mailbox.begin());
                    REQUIRE(workflow.request(DocumentAction::Quit));
                    int executed = 0;
                    int errors = 0;
                    const auto run = [&](const PendingDocumentWork& work) {
                        ++executed;
                        if (work.action == DocumentAction::Quit)
                            return true;
                        CHECK(work.action == action);
                        CHECK(work.path == std::filesystem::path("chosen.scene.gltf"));
                        workflow.setContext(succeeds ? false : dirty, true, false);
                        return succeeds;
                    };
                    const auto report = [&](const std::string& error) {
                        ++errors;
                        CHECK(error == "native failure");
                    };
                    CHECK_FALSE(pumpDocumentWork(workflow, mailbox, run, report));
                    CHECK(executed == 0);
                    if (response == 0)
                        mailbox.post({.path = "chosen.scene.gltf"});
                    else if (response == 1)
                        mailbox.post({});
                    else
                        mailbox.post({.error = "native failure"});
                    const bool quit = pumpDocumentWork(workflow, mailbox, run, report);
                    const bool clean = !dirty || (response == 0 && succeeds);
                    CHECK(quit == clean);
                    CHECK(executed == (response == 0 ? 1 : 0) + (clean ? 1 : 0));
                    CHECK(errors == (response == 2 ? 1 : 0));
                    CHECK_FALSE(mailbox.pending());
                    const auto count = executed;
                    CHECK_FALSE(pumpDocumentWork(workflow, mailbox, run, report));
                    CHECK(executed == count);
                    CHECK(errors == (response == 2 ? 1 : 0));
                    if (!clean) {
                        CHECK(workflow.step() == WorkflowStep::Confirm);
                        CHECK(workflow.action() == DocumentAction::Quit);
                    }
                }
            }
        }
    }
}
