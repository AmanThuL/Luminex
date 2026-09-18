#include "Render/OcclusionCheck.h"
#include <array>
#include <catch2/catch_test_macros.hpp>

using namespace lmx::render;

//======================================================================================================================
TEST_CASE("occlusion reference distinguishes strict visibility from bounded motion recovery",
          "[render][occlusion-check]") {
    OcclusionCheckHistory history;
    std::array observations{OcclusionCheckObservation{81, 7, 31, true},
                            OcclusionCheckObservation{82, 8, 14, false},
                            OcclusionCheckObservation{83, 9, 0, true}};
    auto first = history.observe(1, 22, false, observations);
    REQUIRE(first.passed());
    REQUIRE(first.visibleInstances == 2);
    REQUIRE(first.falselyRejectedInstances == 1);
    REQUIRE(first.falselyRejectedPixels == 31);
    REQUIRE(first.maximumMissingStreak == 1);
    auto second = history.observe(2, 22, false, observations);
    REQUIRE_FALSE(second.passed());
    REQUIRE(second.maximumMissingStreak == 2);
    REQUIRE(second.missing[0].instanceIdentity == 81);
    REQUIRE(second.missing[0].consecutiveFrames == 2);
    history.reset();
    const auto strict = history.observe(1, 22, true, observations);
    REQUIRE_FALSE(strict.passed());
    REQUIRE(strict.maximumMissingStreak == 1);
}

//======================================================================================================================
TEST_CASE("occlusion missing streaks require adjacent checked visible frames and full identity",
          "[render][occlusion-check]") {
    OcclusionCheckHistory history;
    std::array observations{OcclusionCheckObservation{100, 4, 12, true}};
    REQUIRE(history.observe(1, 1, false, observations).maximumMissingStreak == 1);
    observations[0].instanceIdentity = 200;
    REQUIRE(history.observe(2, 1, false, observations).maximumMissingStreak == 1);
    REQUIRE(history.observe(4, 1, false, observations).maximumMissingStreak == 1);
    REQUIRE(history.observe(5, 2, false, observations).maximumMissingStreak == 1);
    observations[0].occluded = false;
    REQUIRE(history.observe(6, 2, false, observations).missing.empty());
    observations[0].occluded = true;
    REQUIRE(history.observe(7, 2, false, observations).maximumMissingStreak == 1);
    observations[0].visiblePixels = 0;
    REQUIRE(history.observe(8, 2, false, observations).missing.empty());
    observations[0].visiblePixels = 12;
    REQUIRE(history.observe(9, 2, false, observations).maximumMissingStreak == 1);
    REQUIRE(history.observe(9, 2, false, observations).maximumMissingStreak == 1);
    history.reset();
    REQUIRE(history.observe(10, 2, false, observations).maximumMissingStreak == 1);
}

//======================================================================================================================
TEST_CASE("occlusion check retains every missing identity and treats invalid references as failure",
          "[render][occlusion-check]") {
    OcclusionCheckHistory history;
    std::array observations{OcclusionCheckObservation{0, 1, 3, true},
                            OcclusionCheckObservation{0, 2, 5, true}};
    auto first = history.observe(1, 1, false, observations);
    REQUIRE(first.missing.size() == 2);
    REQUIRE(first.falselyRejectedPixels == 8);
    observations[0].visiblePixels = 0;
    auto second = history.observe(2, 1, false, observations);
    REQUIRE(second.missing.size() == 1);
    REQUIRE(second.missing[0].instanceRow == 2);
    REQUIRE(second.maximumMissingStreak == 2);
    OcclusionCheckResult corrupt;
    corrupt.invalidReferencePixels = 1;
    REQUIRE_FALSE(corrupt.passed());
    corrupt.invalidReferencePixels = 0;
    corrupt.unmatchedCandidates = 1;
    REQUIRE_FALSE(corrupt.passed());
}
