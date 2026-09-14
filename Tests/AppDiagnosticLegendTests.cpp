#include "App/Model/DiagnosticLegend.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

//======================================================================================================================
TEST_CASE("diagnostic availability follows effective mode and temporal input state", "[app]") {
    using namespace lmx::app;
    using namespace lmx::render;
    constexpr std::array nativeInternals = {TemporalDebugView::RejectionMask,
                                            TemporalDebugView::BlendWeight,
                                            TemporalDebugView::HistoryAge};
    for (const auto view : nativeInternals) {
        REQUIRE_FALSE(
            diagnosticUnavailableReason(view, true, ReconstructionMode::VendorTemporal).empty());
        REQUIRE(diagnosticUnavailableReason(view, true, ReconstructionMode::NativeTaa).empty());
        REQUIRE_FALSE(
            diagnosticUnavailableReason(view, false, ReconstructionMode::NativeTaa).empty());
    }
    constexpr std::array sharedViews = {TemporalDebugView::MotionVectors,
                                        TemporalDebugView::ReprojectionError,
                                        TemporalDebugView::ReprojectedHistory};
    for (const auto view : sharedViews) {
        REQUIRE(
            diagnosticUnavailableReason(view, true, ReconstructionMode::VendorTemporal).empty());
        REQUIRE_FALSE(
            diagnosticUnavailableReason(view, false, ReconstructionMode::VendorTemporal).empty());
    }
    REQUIRE(diagnosticUnavailableReason(TemporalDebugView::Off, false,
                                        ReconstructionMode::VendorTemporal)
                .empty());
    REQUIRE_FALSE(
        diagnosticModeNote(TemporalDebugView::HistoryAge, ReconstructionMode::Raw).empty());
    REQUIRE(
        diagnosticModeNote(TemporalDebugView::HistoryAge, ReconstructionMode::NativeTaa).empty());
}
