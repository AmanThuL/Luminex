//----------------------------------------------------------------------------------------------------------------------
/// @file DocumentDialogMailbox.cpp
/// @brief Transfers native dialog responses without borrowing the shell lifetime.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Scene/DocumentDialogMailbox.h"

namespace lmx::app {

//======================================================================================================================
bool DocumentDialogMailbox::begin() {
    const std::lock_guard lock(m_mutex);
    if (m_pending)
        return false;
    m_pending = true;
    return true;
}

//======================================================================================================================
void DocumentDialogMailbox::post(DocumentDialogResult result) {
    const std::lock_guard lock(m_mutex);
    m_result = std::move(result);
}

//======================================================================================================================
std::optional<DocumentDialogResult> DocumentDialogMailbox::take() {
    const std::lock_guard lock(m_mutex);
    if (!m_result)
        return {};
    auto result = std::move(m_result);
    m_result.reset();
    m_pending = false;
    return result;
}

//======================================================================================================================
bool DocumentDialogMailbox::pending() const {
    const std::lock_guard lock(m_mutex);
    return m_pending;
}
} // namespace lmx::app
