#include "Support/BufferReadWriteGraphSupport.h"

//======================================================================================================================
TEST_CASE("ordinary graph buffer writes order in-place reads across paced frames",
          "[render][graph][device][tracker]") {
#ifdef NDEBUG
    SKIP("the validation tracker runs only in Debug builds");
#else
    auto gpu = createTestGpu({.validation = true});
    const auto run = lmx::test::runBufferReadWriteGraph(*gpu);
    for (uint32_t ordinal = 0; ordinal < lmx::test::kBufferReadWriteFrames; ++ordinal) {
        CAPTURE(ordinal);
        const auto& record = run.records[ordinal];
        REQUIRE(record.frameId == run.submittedFrames[ordinal]);
        REQUIRE(record.frameId == uint64_t{ordinal} + 1);
        REQUIRE(record.debug.resources.size() == 1);
        REQUIRE(record.debug.schedule.passes.size() == (ordinal == 0 ? 2 : 1));
        const auto& compute = record.debug.passes.back();
        REQUIRE(compute.label == "lmx.test.graph.inPlaceAdd");
        REQUIRE(compute.kind == lmx::render::PassKind::Compute);
        REQUIRE(compute.uses.size() == 1);
        REQUIRE(compute.uses[0].role == lmx::render::UseRole::Write);
        REQUIRE(compute.uses[0].resource == 0);
        REQUIRE(compute.uses[0].version == (ordinal == 0 ? 1 : 0));
    }
    REQUIRE(gpu->frameNumber() == run.submittedFrames.back());
    gpu->waitIdle();
    REQUIRE(gpu->completedFrame() == run.submittedFrames.back());
#endif
}
