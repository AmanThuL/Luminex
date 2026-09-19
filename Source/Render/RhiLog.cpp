//----------------------------------------------------------------------------------------------------------------------
/// @file RhiLog.cpp
/// @brief Forwards RHI diagnostic messages into the project log.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/RhiLog.h"

#include "Core/Log.h"
#include "RHI/Message.h"

#include <string>
#include <string_view>

namespace lmx::render {
namespace {

//======================================================================================================================
// The RHI hands over an already-formatted message, so "{}" is the whole format string. The project
// log's own source location therefore names this file rather than the originating RHI call site;
// the message text and severity, which is what readers and the Console filter on, are unchanged.
void forwardMessage(rhi::MessageSeverity severity, std::string_view message, void* /*user*/) {
    const std::string text(message);
    switch (severity) {
    case rhi::MessageSeverity::Info:
        LMX_LOG_INFO("{}", text);
        return;
    case rhi::MessageSeverity::Warning:
        LMX_LOG_WARN("{}", text);
        return;
    case rhi::MessageSeverity::Error:
        LMX_LOG_ERROR("{}", text);
        return;
    }
    LMX_LOG_INFO("{}", text);
}

} // namespace

//======================================================================================================================
void installRhiLogForwarding() {
    rhi::setMessageCallback(&forwardMessage, nullptr);
}

} // namespace lmx::render
