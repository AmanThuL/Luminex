//----------------------------------------------------------------------------------------------------------------------
/// @file MetalCppImpl.cpp
/// @brief Emits the single metal-cpp implementation translation unit the prototype requires.
//----------------------------------------------------------------------------------------------------------------------
// Exactly one translation unit must emit metal-cpp's Objective-C class and selector tables.
// Defining these macros anywhere else in the target violates the ODR. The prototype links
// metal-cpp on its own rather than through the production RHI, so it needs its own copy.
#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
