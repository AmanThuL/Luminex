//----------------------------------------------------------------------------------------------------------------------
/// @file Placeholder.cpp
/// @brief Keeps the NoApiProto target linkable while its interface headers
///        (Experiments/NoApi/Include) are authored (M5.1 stage 1, spec section 5).
//----------------------------------------------------------------------------------------------------------------------

namespace lmx::noapi {

/// Exists only so NoApiProto has a translation unit to compile and NoApiTests has something to
/// link against before the prototype's real sources land under Experiments/NoApi/Source. Delete
/// once a real source file makes this unnecessary.
int placeholderLinkAnchor() { return 0; }

} // namespace lmx::noapi
