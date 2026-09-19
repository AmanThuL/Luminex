#include "Render/RhiLog.h"

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

namespace {

// Gives the test binary the same RHI message routing the editor uses, so a message logged by a
// device under test reaches spdlog rather than raw stderr. Individual message cases install their
// own sink and reset to the stderr default afterwards.
class RhiLogListener : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    //==================================================================================================================
    void testRunStarting(const Catch::TestRunInfo& /*info*/) override {
        lmx::render::installRhiLogForwarding();
    }
};

CATCH_REGISTER_LISTENER(RhiLogListener)

} // namespace
