#pragma once

#include <string>

struct AppConfig {
#ifdef KRIA_BOARD_NAME
    std::string board_name = KRIA_BOARD_NAME;
#else
    std::string board_name = "Kria KV260";
#endif
#ifdef KRIA_UBUNTU_VERSION
    std::string ubuntu_version = KRIA_UBUNTU_VERSION;
#else
    std::string ubuntu_version = "24.04";
#endif
#ifdef KRIA_CAMERA_MODEL
    std::string camera_model = KRIA_CAMERA_MODEL;
#else
    std::string camera_model = "OV9281";
#endif
    std::string mode = "board";
    std::string pixel_format = "MJPG";
    std::string video_device = "/dev/video0";
    std::string host = "192.168.2.1";
    int width = 1280;
    int height = 800;
    int fps = 120;
    std::string bind_address = "0.0.0.0";
    int port = 5600;
    int jpeg_quality = 85;
    int tile_width = 32;
    int tile_height = 32;

    // Loads key=value lines. Missing keys keep the defaults above.
    // Returns false when the file cannot be opened.
    bool load_file(const std::string& path);

    void sanitize();
};
