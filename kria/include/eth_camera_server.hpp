#pragma once

#include "config.hpp"
#include "usb_camera.hpp"

class EthCameraServer {
public:
    explicit EthCameraServer(AppConfig config);

    // Listens until SIGINT/SIGTERM. Returns 0 on a clean stop, 1 on a fatal error.
    int run();

private:
    AppConfig config_;
    UsbCamera camera_;
};
