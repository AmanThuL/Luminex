//----------------------------------------------------------------------------------------------------------------------
/// @file MetricsContextRevision.h
/// @brief Declares monotonic revisions for compatible editor measurement configurations.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <optional>

namespace lmx::app {

/// Assigns a fresh revision whenever an opaque configuration key changes, including re-entry to
/// a previously observed key. The App uses this to reject retirements from an earlier mode visit.
class MetricsContextRevision {
public:
    /// Returns the current revision, advancing on a changed key. Repeated observation of the same
    /// key is idempotent; revisions start at one and persist for this object's lifetime.
    uint64_t observe(uint64_t configurationKey);

private:
    std::optional<uint64_t> m_configurationKey;
    uint64_t m_revision = 0;
};

} // namespace lmx::app
