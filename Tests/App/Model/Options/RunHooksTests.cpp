#include <catch2/catch_test_macros.hpp>

#include "App/Model/Options/RunHooks.h"

#include <cstdint>
#include <limits>

using namespace lmx::app;

//======================================================================================================================
TEST_CASE("frame hooks parse whole decimal frame numbers", "[app][options]") {
    CHECK(parseFrameHook("0") == uint64_t{0});
    CHECK(parseFrameHook("300") == uint64_t{300});
    CHECK(parseFrameHook("18446744073709551615") == std::numeric_limits<uint64_t>::max());
    CHECK_FALSE(parseFrameHook("").has_value());
    CHECK_FALSE(parseFrameHook("-1").has_value());
    CHECK_FALSE(parseFrameHook("30x").has_value());
    CHECK_FALSE(parseFrameHook(" 30").has_value());
    CHECK_FALSE(parseFrameHook("18446744073709551616").has_value());
}

//======================================================================================================================
TEST_CASE("the resize hook shrinks the main window and grows it back", "[app][options]") {
    CHECK(kResizeHookSmallSize != kWindowedSize);
    CHECK_FALSE(resizeHookSize(0, 0).has_value());
    CHECK_FALSE(resizeHookSize(0, 100).has_value());
    CHECK_FALSE(resizeHookSize(300, 299).has_value());
    CHECK(resizeHookSize(300, 300) == kResizeHookSmallSize);
    CHECK_FALSE(resizeHookSize(300, 301).has_value());
    CHECK_FALSE(resizeHookSize(300, 300 + kResizeHookHoldFrames - 1).has_value());
    CHECK(resizeHookSize(300, 300 + kResizeHookHoldFrames) == kWindowedSize);
    CHECK_FALSE(resizeHookSize(300, 300 + kResizeHookHoldFrames + 1).has_value());
    CHECK_FALSE(resizeHookSize(300, 300 + 2 * kResizeHookHoldFrames).has_value());

    constexpr uint64_t last = std::numeric_limits<uint64_t>::max();
    CHECK(resizeHookSize(last, last) == kResizeHookSmallSize);
    CHECK_FALSE(resizeHookSize(last, 0).has_value());
}
