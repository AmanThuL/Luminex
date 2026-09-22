#include "App/Model/Rendering/Temporal/DiagnosticLegend.h"

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

//======================================================================================================================
TEST_CASE("lighting legends distinguish list occupancy from missing contribution",
          "[app][lighting-display]") {
    using namespace lmx::app;
    using enum lmx::engine::LightDebugView;
    REQUIRE(diagnosticLegend(Count).description.find("65-128 red") != std::string_view::npos);
    REQUIRE(diagnosticLegend(Overflow).description.find("25%") != std::string_view::npos);
    REQUIRE(diagnosticLegend(Missed).description.find("list error") != std::string_view::npos);
    REQUIRE(diagnosticLegend(Missed).description.find("expected loss") != std::string_view::npos);
}
