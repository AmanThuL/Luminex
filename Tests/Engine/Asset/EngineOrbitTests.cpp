#include "Support/EngineSceneTestSupport.h"

//======================================================================================================================
TEST_CASE("sampleOrbit is periodic and closed-form about its centre", "[scene]") {
    LightOrbitTrack track{.light = 0,
                          .centre = glm::vec3(1.0f, 2.0f, 3.0f),
                          .axis = glm::vec3(0.0f, 0.0f, 1.0f),
                          .radius = 5.0f,
                          .phase = 0.0f,
                          .period = 8.0f};

    const glm::vec3 start = sampleOrbit(track, 0.0f);
    const glm::vec3 quarter = sampleOrbit(track, 2.0f); // period / 4
    const glm::vec3 full = sampleOrbit(track, 8.0f);    // period

    // At t == 0 the sample sits `radius` from the centre, along the deterministic basis's first
    // axis: axis is +Z here, so world up (+Y) is the reference and u is world +X.
    REQUIRE(near3(start - track.centre, glm::vec3(track.radius, 0.0f, 0.0f)));
    REQUIRE(Catch::Approx(glm::length(quarter - track.centre)).margin(1e-4f) == track.radius);
    REQUIRE(Catch::Approx(glm::dot(quarter - track.centre, start - track.centre)).margin(1e-3f) ==
            0.0f);
    // A full revolution returns to the starting position.
    REQUIRE(near3(full, start));
}

//======================================================================================================================
TEST_CASE("sampleOrbit holds position fixed when the period is nonpositive", "[scene]") {
    LightOrbitTrack track{.light = 0,
                          .centre = glm::vec3(0.0f),
                          .axis = glm::vec3(0.0f, 0.0f, 1.0f),
                          .radius = 2.0f,
                          .phase = glm::half_pi<float>(),
                          .period = 0.0f};

    const glm::vec3 early = sampleOrbit(track, 0.0f);
    const glm::vec3 later = sampleOrbit(track, 100.0f);
    REQUIRE(near3(early, later));
}

//======================================================================================================================
TEST_CASE("sampleOrbit wraps far-future seconds onto the same angle as their reduced phase",
          "[scene]") {
    LightOrbitTrack track{.light = 0,
                          .centre = glm::vec3(1.0f, 2.0f, 3.0f),
                          .axis = glm::vec3(0.0f, 0.0f, 1.0f),
                          .radius = 5.0f,
                          .phase = 0.0f,
                          .period = 8.0f};

    // 1000 whole periods plus a quarter turn should land exactly where a bare quarter turn does;
    // without wrapping seconds onto one period first, float precision at this magnitude coarsens
    // the angle well past a tight tolerance.
    const glm::vec3 nearPosition = sampleOrbit(track, 2.0f); // period / 4
    const glm::vec3 farPosition = sampleOrbit(track, 1000.0f * track.period + 2.0f);
    REQUIRE(near3(nearPosition, farPosition));
}
