// metal-cpp is header-only apart from its Objective-C class and selector tables, which
// exactly one translation unit in the program must emit. Defining these macros in a
// second TU is an ODR violation that shows up as duplicate symbols at link time.
//
// Verified against the vendored headers (release/metal-cpp_macOS26.4_iOS26.4): the only
// gates that exist are NS_PRIVATE_IMPLEMENTATION (Foundation/NSPrivate.hpp),
// CA_PRIVATE_IMPLEMENTATION (QuartzCore/CAPrivate.hpp), MTL_PRIVATE_IMPLEMENTATION
// (Metal/MTLPrivate.hpp) and MTLFX_PRIVATE_IMPLEMENTATION (MetalFX, unused here).
// There is no separate MTL4 gate -- every MTL4 class lives behind MTL_PRIVATE_IMPLEMENTATION.
#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
