#include "Core/Log.h"
#include "RHI/RHI.h"

int main() {
    lmx::log::init();

    auto device = lmx::rhi::createDevice();
    if (!device) {
        LMX_LOG_ERROR("createDevice failed: {}", device.error().message);
        return 1;
    }

    LMX_LOG_INFO("Metal 4 device: {}", (*device)->deviceName());
    return 0;
}
