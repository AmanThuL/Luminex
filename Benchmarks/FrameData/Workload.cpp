//----------------------------------------------------------------------------------------------------------------------
/// @file Workload.cpp
/// @brief Implements workload lookup by name.
//----------------------------------------------------------------------------------------------------------------------
#include "Workload.h"

namespace lmx::bench {

//======================================================================================================================
const WorkloadSpec* findWorkload(std::string_view name) {
    for (const WorkloadSpec& spec : kWorkloads) {
        if (spec.name == name) {
            return &spec;
        }
    }
    return nullptr;
}

} // namespace lmx::bench
