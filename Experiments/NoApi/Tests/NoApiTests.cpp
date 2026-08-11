//----------------------------------------------------------------------------------------------------------------------
/// @file NoApiTests.cpp
/// @brief Prototype-only smoke test skeleton for NoApiProto (M5.1 stage 1).
//----------------------------------------------------------------------------------------------------------------------

#include <catch2/catch_test_macros.hpp>

// Deliberately does not include anything from Experiments/NoApi/Include yet: the prototype's
// headers are authored in parallel with this skeleton (spec section 5), and this test's only job
// right now is to prove NoApiTests links against NoApiProto and runs under Catch2. Real coverage
// -- an offscreen render and readback exercising each of the six interface areas -- is stage 2's
// deliverable (docs/plans/2026-08-12-m5.1-rhi-execution-model.md).
TEST_CASE("NoApiTests links and runs", "[noapi]") { REQUIRE(1 + 1 == 2); }
