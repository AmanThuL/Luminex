//----------------------------------------------------------------------------------------------------------------------
/// @file ModelTests.cpp
/// @brief Checks deterministic inputs, visibility, geometry, packing, and ABI limits.
//----------------------------------------------------------------------------------------------------------------------
#include "Model/Workload.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace sub = lmx::experimental::submission;
namespace {

struct Rectangle {
    double left, right, bottom, top;
};

struct Event {
    double x;
    bool enters;
    size_t id;
};

class CommaPunct final : public std::numpunct<char> {
protected:
    char do_decimal_point() const override;
    char do_thousands_sep() const override;
    std::string do_grouping() const override;
};

//======================================================================================================================
char CommaPunct::do_decimal_point() const {
    return ',';
}

//======================================================================================================================
char CommaPunct::do_thousands_sep() const {
    return '.';
}

//======================================================================================================================
std::string CommaPunct::do_grouping() const {
    return "\3";
}

//======================================================================================================================
Rectangle project(const sub::Instance& instance, const sub::Params& params) {
    const auto& m = params.viewProjection;
    const double w = double{m[11]} * instance.z + m[15];
    return {(double{m[0]} * (double{instance.x} - instance.halfExtent) + m[12]) / w,
            (double{m[0]} * (double{instance.x} + instance.halfExtent) + m[12]) / w,
            (double{m[5]} * (double{instance.y} - instance.halfExtent) + m[13]) / w,
            (double{m[5]} * (double{instance.y} + instance.halfExtent) + m[13]) / w};
}

//======================================================================================================================
bool projectedNonoverlap(const sub::FrameInput& frame) {
    std::vector<Rectangle> rectangles;
    std::vector<Event> events;
    for (const auto& instance : frame.instances) {
        const auto rect = project(instance, frame.params);
        const size_t id = rectangles.size();
        rectangles.push_back(rect);
        events.push_back({rect.left, true, id});
        events.push_back({rect.right, false, id});
    }
    std::ranges::sort(events, [](const Event& a, const Event& b) {
        return a.x != b.x ? a.x < b.x : a.enters < b.enters;
    });
    std::map<std::pair<double, size_t>, double> active;
    for (const auto& event : events) {
        const auto& rect = rectangles[event.id];
        const auto key = std::pair{rect.bottom, event.id};
        if (!event.enters) {
            active.erase(key);
            continue;
        }
        const auto next = active.lower_bound(key);
        if (next != active.end() && next->first.first < rect.top) {
            return false;
        }
        if (next != active.begin() && std::prev(next)->second > rect.bottom) {
            return false;
        }
        active.emplace(key, rect.top);
    }
    return active.empty();
}

//======================================================================================================================
bool floatVisible(const sub::Instance& instance, const sub::Params& params) {
    for (const auto& plane : params.planes) {
        const float distance =
            plane[0] * instance.x + plane[1] * instance.y + plane[2] * instance.z + plane[3];
        if (distance < -instance.radius - params.epsilon) {
            return false;
        }
    }
    return true;
}

//======================================================================================================================
sub::Params boxPlanes() {
    sub::Params params;
    params.planes = {
        {{1, 0, 0, 1}, {-1, 0, 0, 1}, {0, 1, 0, 1}, {0, -1, 0, 1}, {0, 0, 1, 1}, {0, 0, -1, 1}}};
    return params;
}

} // namespace

//======================================================================================================================
TEST_CASE("Submission matrix has twenty stable scored cases and separate correctness aliases",
          "[submission][model]") {
    const auto matrix = sub::caseMatrix();
    REQUIRE(matrix.size() == 20);
    std::set<std::string> ids;
    size_t index = 0;
    for (uint32_t count : {1024U, 16384U, 65536U}) {
        for (uint32_t triangles : {2U, 32U}) {
            for (double fraction : {0.1, 0.5, 1.0}) {
                const auto& spec = matrix[index++];
                CHECK(spec.count == count);
                CHECK(spec.triangles == triangles);
                CHECK(spec.visibleFraction == fraction);
                CHECK(spec.bins == 1);
            }
        }
    }
    CHECK(matrix[18].id == "n16384-t2-v50-b16");
    CHECK(matrix[19].id == "n16384-t2-v50-b64");
    for (const auto& spec : matrix) {
        CHECK(sub::validateCase(spec));
        CHECK(ids.insert(spec.id).second);
        REQUIRE(sub::findCase(spec.id));
        CHECK(sub::findCase(spec.id)->id == spec.id);
    }
    for (const auto& id : {"empty", "single", "tail"}) {
        auto spec = sub::findCase(id);
        REQUIRE(spec);
        CHECK_FALSE(ids.contains(id));
        REQUIRE(sub::makeFrame(*spec, 0));
        CHECK(sub::manifestJson(*spec).find("\"scoredMatrixCase\":false") != std::string::npos);
    }
    CHECK(sub::findCase("empty")->count == 0);
    CHECK(sub::findCase("single")->count == 1);
    CHECK(sub::findCase("tail")->count == 257);
    CHECK_FALSE(sub::findCase("EMPTY"));
    CHECK_FALSE(sub::findCase("n1024-t2-v10-b16"));
}

//======================================================================================================================
TEST_CASE("Submission cases reject invalid fractions geometry bins and indexing before allocation",
          "[submission][model]") {
    const sub::Case valid{"validation", 257, 32, 64, 0.5};
    REQUIRE(sub::validateCase(valid));
    for (double fraction :
         {-0.1, 1.1, std::numeric_limits<double>::quiet_NaN(),
          std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()}) {
        auto bad = valid;
        bad.visibleFraction = fraction;
        CHECK_FALSE(sub::validateCase(bad));
        CHECK_FALSE(sub::makeFrame(bad, 0));
        const auto manifest = sub::manifestJson(bad);
        CHECK(manifest.find("\"valid\":false") != std::string::npos);
        CHECK(manifest.find("NaN") == std::string::npos);
        CHECK(manifest.find("Infinity") == std::string::npos);
    }
    for (uint32_t triangles : {0U, 1U, 3U, 31U, 33U, UINT32_MAX}) {
        auto bad = valid;
        bad.triangles = triangles;
        CHECK_FALSE(sub::makeFrame(bad, 0));
    }
    for (uint32_t bins : {0U, 65U, UINT32_MAX}) {
        auto bad = valid;
        bad.bins = bins;
        CHECK_FALSE(sub::makeFrame(bad, 0));
    }
    auto bad = valid;
    bad.id.clear();
    CHECK_FALSE(sub::validateCase(bad));
    bad = valid;
    bad.count = sub::kMaxInstances + 1;
    CHECK_FALSE(sub::makeFrame(bad, 0));
    bad.count = UINT32_MAX / (3 * valid.triangles) + 1;
    const auto overflow = sub::validateCase(bad);
    REQUIRE_FALSE(overflow);
    CHECK(overflow.error().find("vertex") != std::string::npos);
    bad.count = UINT32_MAX;
    CHECK_FALSE(sub::makeFrame(bad, 0));
}

//======================================================================================================================
TEST_CASE("Submission inputs preserve exact visibility safe margins and contiguous bins",
          "[submission][model]") {
    for (const auto& spec : sub::caseMatrix()) {
        for (uint32_t phase = 0; phase < sub::kPhaseCount; ++phase) {
            CAPTURE(spec.id, phase);
            auto input = sub::makeFrame(spec, phase * sub::kFramesPerPhase);
            REQUIRE(input);
            const auto& frame = *input;
            CHECK(frame.visibleIds.size() == std::floor(spec.count * spec.visibleFraction));
            CHECK(frame.binOffsets.size() == spec.bins + 1);
            CHECK(frame.binOffsets.front() == 0);
            CHECK(frame.binOffsets.back() == frame.visibleIds.size());
            CHECK(std::ranges::is_sorted(frame.visibleIds));
            bool valid = true;
            for (uint32_t bin = 0; bin < spec.bins; ++bin) {
                const uint32_t begin = uint64_t{bin} * spec.count / spec.bins;
                const uint32_t end = uint64_t{bin + 1} * spec.count / spec.bins;
                for (uint32_t id = begin; id < end; ++id) {
                    const auto& instance = frame.instances[id];
                    valid &=
                        instance.bin == bin && instance.padding0 == 0 && instance.padding1 == 0;
                    valid &= sub::sphereVisible(instance, frame.params) == (frame.bitmap[id] == 1);
                    valid &= floatVisible(instance, frame.params) == (frame.bitmap[id] == 1);
                    for (const auto& plane : frame.params.planes) {
                        const double distance = double{plane[0]} * instance.x +
                                                double{plane[1]} * instance.y +
                                                double{plane[2]} * instance.z + plane[3];
                        valid &= std::abs(distance + instance.radius) >= sub::kScoredBoundaryMargin;
                    }
                }
                for (uint32_t offset = frame.binOffsets[bin]; offset < frame.binOffsets[bin + 1];
                     ++offset) {
                    const uint32_t id = frame.visibleIds[offset];
                    valid &= id >= begin && id < end && frame.bitmap[id] == 1;
                }
            }
            CHECK(valid);
        }
    }
}

//======================================================================================================================
TEST_CASE("Submission replay changes IDs every phase and repeats after 256 frames",
          "[submission][model]") {
    const sub::Case spec{"replay", 257, 2, 16, 0.5};
    std::array<std::string, sub::kPhaseCount> hashes;
    std::set<std::vector<uint32_t>> visibleSets;
    for (uint32_t frame = 0; frame < sub::kReplayFrames; ++frame) {
        const auto input = sub::makeFrame(spec, frame);
        REQUIRE(input);
        const uint32_t phase = frame / sub::kFramesPerPhase;
        if (frame % sub::kFramesPerPhase == 0) {
            hashes[phase] = sub::frameHash(*input);
            CHECK(visibleSets.insert(input->visibleIds).second);
        }
        CHECK(sub::frameHash(*input) == hashes[phase]);
        const auto replay = sub::makeFrame(spec, frame + sub::kReplayFrames);
        REQUIRE(replay);
        CHECK(sub::frameHash(*replay) == hashes[phase]);
    }
    auto wrapped = sub::makeFrame(spec, UINT32_MAX);
    REQUIRE(wrapped);
    CHECK(sub::frameHash(*wrapped) == hashes.back());
}

//======================================================================================================================
TEST_CASE("Submission empty single and dispatch-tail frames handle empty material bins",
          "[submission][model]") {
    for (uint32_t count : {0U, 1U, 257U}) {
        for (uint32_t bins : {1U, 16U, 64U}) {
            for (double fraction : {0.0, 0.1, 0.5, 1.0}) {
                const sub::Case spec{"correctness", count, 2, bins, fraction};
                for (uint32_t phase = 0; phase < sub::kPhaseCount; ++phase) {
                    CAPTURE(count, bins, fraction, phase);
                    const auto input = sub::makeFrame(spec, phase * sub::kFramesPerPhase);
                    REQUIRE(input);
                    CHECK(input->instances.size() == count);
                    CHECK(input->bitmap.size() == count);
                    CHECK(input->visibleIds.size() == std::floor(count * fraction));
                    CHECK(input->binOffsets.size() == bins + 1);
                    CHECK(input->binOffsets.back() == input->visibleIds.size());
                    CHECK(sub::classify(*input) == input->visibleIds);
                    for (uint32_t bin = 0; bin < bins; ++bin) {
                        const uint32_t begin = uint64_t{bin} * count / bins;
                        const uint32_t end = uint64_t{bin + 1} * count / bins;
                        for (uint32_t id = begin; id < end; ++id) {
                            CHECK(input->instances[id].bin == bin);
                        }
                    }
                }
            }
        }
    }
}

//======================================================================================================================
TEST_CASE("Submission oracle ignores cached visibility and packs bin then object order",
          "[submission][model]") {
    auto input = sub::makeFrame({"packing", 5, 2, 3, 1}, 0);
    REQUIRE(input);
    for (uint32_t id = 0; id < 5; ++id) {
        input->instances[id].bin = std::array{2U, 0U, 1U, 0U, 2U}[id];
    }
    input->bitmap.assign(5, 0);
    input->visibleIds = {4};
    input->binOffsets = {0, 0, 0, 1};
    CHECK(sub::classify(*input) == std::vector<uint32_t>{1, 3, 2, 0, 4});
    input->instances[3].x = 1000;
    CHECK(sub::classify(*input) == std::vector<uint32_t>{1, 2, 0, 4});
    input->instances[0].bin = 3;
    CHECK(sub::classify(*input).empty());
}

//======================================================================================================================
TEST_CASE("Submission conservative boundary epsilon applies on all six normalized planes",
          "[submission][model]") {
    for (size_t axis = 0; axis < 3; ++axis) {
        for (float sign : {-1.F, 1.F}) {
            auto params = boxPlanes();
            for (float offset : {-4 * sub::kBoundaryEpsilon, 0.F, 4 * sub::kBoundaryEpsilon}) {
                std::array<float, 3> center{};
                center[axis] = sign * (1.25F + offset);
                sub::Instance instance{center[0], center[1], center[2], 0.1F, 0.25F, 0};
                CHECK(sub::sphereVisible(instance, params) == (offset <= 0));
                params.epsilon = sub::kBoundaryEpsilon;
                CHECK(sub::sphereVisible(instance, params) == (offset <= 0));
                params.epsilon = 0;
            }
            std::array<float, 3> center{};
            center[axis] = sign * (1.25F + sub::kBoundaryEpsilon / 2);
            const sub::Instance close{center[0], center[1], center[2], 0.1F, 0.25F, 0};
            CHECK_FALSE(sub::sphereVisible(close, params));
            params.epsilon = sub::kBoundaryEpsilon;
            CHECK(sub::sphereVisible(close, params));
        }
    }
}

//======================================================================================================================
TEST_CASE("Submission oracle rejects nonfinite camera bounds epsilon and malformed planes",
          "[submission][model]") {
    const sub::Instance valid{0, 0, 0, 0.1F, 0.25F, 0};
    const auto params = boxPlanes();
    REQUIRE(sub::sphereVisible(valid, params));
    for (float invalid :
         {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
          -std::numeric_limits<float>::infinity()}) {
        for (auto member : {&sub::Instance::x, &sub::Instance::y, &sub::Instance::z,
                            &sub::Instance::halfExtent, &sub::Instance::radius}) {
            auto instance = valid;
            instance.*member = invalid;
            CHECK_FALSE(sub::sphereVisible(instance, params));
        }
        for (size_t plane = 0; plane < 6; ++plane) {
            for (size_t component = 0; component < 4; ++component) {
                auto bad = params;
                bad.planes[plane][component] = invalid;
                CHECK_FALSE(sub::sphereVisible(valid, bad));
            }
        }
        auto bad = params;
        bad.epsilon = invalid;
        CHECK_FALSE(sub::sphereVisible(valid, bad));
        bad = params;
        bad.viewProjection[0] = invalid;
        CHECK_FALSE(sub::sphereVisible(valid, bad));
    }
    for (float radius : {-1.F, 0.F, 0.1F}) {
        auto instance = valid;
        instance.radius = radius;
        CHECK_FALSE(sub::sphereVisible(instance, params));
    }
    for (float extent : {-1.F, 0.F}) {
        auto instance = valid;
        instance.halfExtent = extent;
        CHECK_FALSE(sub::sphereVisible(instance, params));
    }
    for (float scale : {0.F, 0.5F, 2.F}) {
        auto bad = params;
        bad.planes[0][0] = scale;
        CHECK_FALSE(sub::sphereVisible(valid, bad));
    }
    auto bad = params;
    bad.epsilon = -sub::kBoundaryEpsilon;
    CHECK_FALSE(sub::sphereVisible(valid, bad));
}

//======================================================================================================================
TEST_CASE("Submission projected quads are disjoint and reverse depth stays distinct",
          "[submission][model]") {
    for (const auto& spec : sub::caseMatrix()) {
        const auto input = sub::makeFrame(spec, 96);
        REQUIRE(input);
        CAPTURE(spec.id);
        CHECK(projectedNonoverlap(*input));
        std::set<float> depths;
        bool valid = true;
        for (uint32_t id = 0; id < spec.count; ++id) {
            const auto& instance = input->instances[id];
            valid &= depths.insert(instance.z).second;
            const auto rect = project(instance, input->params);
            if (input->bitmap[id] != 0) {
                valid &= rect.left > -1 && rect.right < 1 && rect.bottom > -1 && rect.top < 1;
            } else {
                valid &= rect.left > 1;
            }
            const auto& m = input->params.viewProjection;
            const double depth = (double{m[10]} * instance.z + m[14]) / -instance.z;
            valid &= depth > 0 && depth < 1;
        }
        CHECK(valid);
    }
    const auto input = sub::makeFrame(*sub::findCase("single"), 0);
    REQUIRE(input);
    const auto& m = input->params.viewProjection;
    CHECK(std::abs((-m[10] + m[14]) - 1.0) < 1e-6);
    CHECK(std::abs((-64.0 * m[10] + m[14]) / 64.0) < 1e-6);
}

//======================================================================================================================
TEST_CASE("Submission tessellations have equal silhouettes and cover the quad exactly once",
          "[submission][model]") {
    for (uint32_t triangles : {2U, 32U}) {
        const uint32_t side = triangles == 32 ? 4 : 1;
        constexpr std::array<std::array<uint32_t, 2>, 6> corners = {
            {{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}}};
        double area = 0;
        double minimum = 1;
        double maximum = -1;
        for (uint32_t triangle = 0; triangle < triangles; ++triangle) {
            std::array<std::array<double, 2>, 3> points{};
            for (uint32_t vertex = 0; vertex < 3; ++vertex) {
                const uint32_t localVertex = triangle * 3 + vertex;
                const uint32_t cell = localVertex / 6;
                const auto corner = corners[localVertex % 6];
                points[vertex] = {2.0 * (cell % side + corner[0]) / side - 1,
                                  2.0 * (cell / side + corner[1]) / side - 1};
                for (double coordinate : points[vertex]) {
                    minimum = std::min(minimum, coordinate);
                    maximum = std::max(maximum, coordinate);
                }
            }
            const double cross = (points[1][0] - points[0][0]) * (points[2][1] - points[0][1]) -
                                 (points[1][1] - points[0][1]) * (points[2][0] - points[0][0]);
            CHECK(cross > 0);
            area += cross / 2;
        }
        CHECK(area == 4);
        CHECK(minimum == -1);
        CHECK(maximum == 1);
    }
    auto coarse = sub::makeFrame({"coarse", 257, 2, 16, 0.5}, 64);
    auto fine = sub::makeFrame({"fine", 257, 32, 16, 0.5}, 64);
    REQUIRE(coarse);
    REQUIRE(fine);
    for (size_t id = 0; id < coarse->instances.size(); ++id) {
        CHECK(coarse->instances[id].x == fine->instances[id].x);
        CHECK(coarse->instances[id].y == fine->instances[id].y);
        CHECK(coarse->instances[id].z == fine->instances[id].z);
        CHECK(coarse->instances[id].halfExtent == fine->instances[id].halfExtent);
        CHECK(coarse->instances[id].radius == fine->instances[id].radius);
    }
}

//======================================================================================================================
TEST_CASE("Submission generator accepts its capacity without duplicate float depths",
          "[submission][model]") {
    const sub::Case spec{"capacity", sub::kMaxInstances, 32, 64, 0.5};
    auto input = sub::makeFrame(spec, 224);
    REQUIRE(input);
    CHECK(input->visibleIds.size() == sub::kMaxInstances / 2);
    std::vector<float> depths;
    depths.reserve(spec.count);
    for (const auto& instance : input->instances) {
        depths.push_back(instance.z);
    }
    std::ranges::sort(depths);
    CHECK(std::ranges::adjacent_find(depths) == depths.end());
    const uint64_t lastVertex =
        uint64_t{spec.count - 1} * 3 * spec.triangles + 3 * spec.triangles - 1;
    CHECK(lastVertex < UINT32_MAX);
}

//======================================================================================================================
TEST_CASE("Submission hashes cover every uploaded and expected field without object padding",
          "[submission][model]") {
    const auto input = sub::makeFrame({"hash", 1, 2, 1, 1}, 0);
    REQUIRE(input);
    const auto original = sub::frameHash(*input);
    CHECK(original == "b17b8076476e5fd4");
    CHECK(original.size() == 16);
    CHECK(original.find_first_not_of("0123456789abcdef") == std::string::npos);
    CHECK(sub::frameHash(*sub::makeFrame({"different-id", 1, 2, 1, 1}, 0)) == original);
    auto changed = *input;
    changed.params.viewProjection[0] = 2;
    CHECK(sub::frameHash(changed) != original);
    changed = *input;
    changed.params.planes[0][3] = 1;
    CHECK(sub::frameHash(changed) != original);
    changed = *input;
    changed.params.suite = 1;
    CHECK(sub::frameHash(changed) != original);
    changed = *input;
    changed.params.epsilon = sub::kBoundaryEpsilon;
    CHECK(sub::frameHash(changed) != original);
    changed = *input;
    changed.instances[0].padding1 = 1;
    CHECK(sub::frameHash(changed) != original);
    changed = *input;
    changed.instances[0].x = 1;
    CHECK(sub::frameHash(changed) != original);
    changed = *input;
    changed.instances[0].bin = 1;
    CHECK(sub::frameHash(changed) != original);
    changed = *input;
    changed.bitmap[0] = 0;
    CHECK(sub::frameHash(changed) != original);
    changed = *input;
    changed.visibleIds.clear();
    CHECK(sub::frameHash(changed) != original);
    changed = *input;
    changed.binOffsets.back() = 0;
    CHECK(sub::frameHash(changed) != original);
}

//======================================================================================================================
TEST_CASE("Submission manifest resolves phase transforms and is deterministic across locales",
          "[submission][model]") {
    const sub::Case spec{"quoted\"\\\n\t", 257, 32, 16, 0.5};
    const auto manifest = sub::manifestJson(spec);
    CHECK(manifest == sub::manifestJson(spec));
    CHECK(manifest.find("\"id\":\"quoted\\\"\\\\\\u000a\\u0009\"") != std::string::npos);
    CHECK(manifest.find("\"phaseZeroTransforms\":[[") != std::string::npos);
    CHECK(manifest.find("\"binCandidateRanges\":[[0,16],[16,32]") != std::string::npos);
    CHECK(manifest.find("\"actualVisibleCount\":128") != std::string::npos);
    CHECK(manifest.find("\"replayFrames\":256") != std::string::npos);
    CHECK(manifest.find("\"schemaVersion\":1") != std::string::npos);
    CHECK(manifest.find("\"hashIsCryptographic\":false") != std::string::npos);
    const auto first = sub::makeFrame(spec, 0);
    REQUIRE(first);
    for (uint32_t phase = 0; phase < sub::kPhaseCount; ++phase) {
        const auto input = sub::makeFrame(spec, phase * sub::kFramesPerPhase);
        REQUIRE(input);
        CHECK(manifest.find(sub::frameHash(*input)) != std::string::npos);
        for (uint32_t id = 0; id < spec.count; ++id) {
            const uint32_t source = (id + phase * (spec.count / sub::kPhaseCount)) % spec.count;
            CHECK(input->instances[id].x == first->instances[source].x);
            CHECK(input->instances[id].radius == first->instances[source].radius);
            CHECK(input->bitmap[id] == first->bitmap[source]);
        }
    }
    const auto previous = std::locale::global(std::locale(std::locale::classic(), new CommaPunct));
    const auto localizedManifest = sub::manifestJson(spec);
    const auto localizedHash = sub::frameHash(*first);
    std::locale::global(previous);
    CHECK(localizedManifest == manifest);
    CHECK(localizedHash == sub::frameHash(*first));
}

//======================================================================================================================
TEST_CASE("Submission largest manifest stores resolved transforms once with all phase hashes",
          "[submission][model]") {
    const auto spec = sub::findCase("n65536-t32-v50-b1");
    REQUIRE(spec);
    const auto manifest = sub::manifestJson(*spec);
    INFO("Largest-case manifest bytes: " << manifest.size());
    const auto table = manifest.find("\"phaseZeroTransforms\":");
    REQUIRE(table != std::string::npos);
    CHECK(manifest.find("\"phaseZeroTransforms\":", table + 1) == std::string::npos);
    CHECK(manifest.find("\"scoredMatrixCase\":true") != std::string::npos);
    for (uint32_t phase = 0; phase < sub::kPhaseCount; ++phase) {
        const auto input = sub::makeFrame(*spec, phase * sub::kFramesPerPhase);
        REQUIRE(input);
        CHECK(manifest.find(sub::frameHash(*input)) != std::string::npos);
    }
}

//======================================================================================================================
TEST_CASE("Submission CLI names roundtrip exactly and invalid enumerations remain unknown",
          "[submission][model]") {
    for (sub::Variant variant :
         {sub::Variant::Direct, sub::Variant::CpuIndirect, sub::Variant::GpuArgs,
          sub::Variant::GpuIcb, sub::Variant::Batched}) {
        REQUIRE(sub::parseVariant(sub::name(variant)));
        CHECK(*sub::parseVariant(sub::name(variant)) == variant);
    }
    for (sub::Suite suite : {sub::Suite::S, sub::Suite::E}) {
        REQUIRE(sub::parseSuite(sub::name(suite)));
        CHECK(*sub::parseSuite(sub::name(suite)) == suite);
    }
    for (sub::Lane lane : {sub::Lane::Headline, sub::Lane::GpuSpan, sub::Lane::Stages}) {
        REQUIRE(sub::parseLane(sub::name(lane)));
        CHECK(*sub::parseLane(sub::name(lane)) == lane);
    }
    for (auto invalid : {"", "all", "DIRECT", "gpu_args", "direct ", "e", "GPU-SPAN"}) {
        CHECK_FALSE(sub::parseVariant(invalid));
        CHECK_FALSE(sub::parseSuite(invalid));
        CHECK_FALSE(sub::parseLane(invalid));
    }
    CHECK(sub::name(static_cast<sub::Suite>(-1)) == "unknown");
    CHECK(sub::name(static_cast<sub::Variant>(-1)) == "unknown");
    CHECK(sub::name(static_cast<sub::Lane>(-1)) == "unknown");
}
