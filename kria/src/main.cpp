#include "config.hpp"
#include "eth_camera_server.hpp"
#include "logging.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

void print_usage() {
    std::cout
        << "kria_eth_camera — USB OV9281 to Ethernet camera\n"
        << "Board target: Kria KV260, Ubuntu 24.04\n\n"
        << "Usage:\n"
        << "  kria_eth_camera --config config/board.conf\n\n"
        << "Settings live in the config file, not on the command line.\n"
        << "  config/board.conf    Kria KV260, OV9281, MJPEG 1280x800\n"
        << "  config/laptop.conf   this laptop, LG Camera, YUY2 640x480, localhost:5600\n\n"
        << "Optional overrides: [--device PATH] [--bind ADDR] [--port N]\n";
}

bool file_exists(const std::string& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

std::string find_config() {
    const char* from_env = std::getenv("KRIA_CONFIG");
    const std::vector<std::string> candidates = {
        from_env ? from_env : "",
        "config/board.conf",
        "config/laptop.conf",
        "../config/board.conf",
        "../config/laptop.conf",
        "../../config/board.conf",
        "../../config/laptop.conf",
        "/etc/kria-ethernet-camera/board.conf",
        "/usr/local/etc/kria-ethernet-camera/board.conf",
    };
    for (const std::string& path : candidates) {
        if (!path.empty() && file_exists(path)) {
            return path;
        }
    }
    return {};
}

}  // namespace

int main(int argc, char** argv) {
    std::string config_path;
    std::string device_override;
    std::string bind_override;
    int port_override = -1;

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        auto need_value = [&](const char* name) -> const char* {
            if (index + 1 >= argc) {
                log_line(std::string("Missing value for ") + name);
                print_usage();
                return nullptr;
            }
            return argv[++index];
        };

        if (arg == "-h" || arg == "--help") {
            print_usage();
            return 0;
        }
        if (arg == "--mode") {
            const char* value = need_value("--mode");
            if (value == nullptr) {
                return 2;
            }
            if (std::string(value) != "board") {
                log_line(std::string(value) +
                         " is the laptop mode. On this board use --mode board. On the laptop run: python pc/mjpeg_pipeline.py --mode sim");
                return 2;
            }
            continue;
        }
        if (arg == "--config") {
            const char* value = need_value("--config");
            if (value == nullptr) {
                return 2;
            }
            config_path = value;
        } else if (arg == "--device") {
            const char* value = need_value("--device");
            if (value == nullptr) {
                return 2;
            }
            device_override = value;
        } else if (arg == "--bind") {
            const char* value = need_value("--bind");
            if (value == nullptr) {
                return 2;
            }
            bind_override = value;
        } else if (arg == "--port") {
            const char* value = need_value("--port");
            if (value == nullptr) {
                return 2;
            }
            try {
                port_override = std::stoi(value);
            } catch (const std::exception&) {
                log_line("Invalid port");
                return 2;
            }
        } else {
            log_line("Unknown argument " + arg);
            print_usage();
            return 2;
        }
    }

    if (config_path.empty()) {
        config_path = find_config();
    }

    AppConfig config;
    if (!config_path.empty()) {
        if (!config.load_file(config_path)) {
            log_line("Cannot read config " + config_path + ", using project defaults");
        } else {
            log_line("Loaded config " + config_path);
        }
    } else {
        log_line("No board.conf found, using project defaults");
        config.sanitize();
    }

    if (!device_override.empty()) {
        config.video_device = device_override;
    }
    if (!bind_override.empty()) {
        config.bind_address = bind_override;
    }
    if (port_override > 0) {
        config.port = port_override;
    }
    config.sanitize();

    log_line("Board name: " + config.board_name);
    log_line("Ubuntu version: " + config.ubuntu_version);
    log_line("Camera: " + config.camera_model + " " + config.video_device + " " +
             std::to_string(config.width) + "x" + std::to_string(config.height) + " @" +
             std::to_string(config.fps) + " fps");

    EthCameraServer server(std::move(config));
    return server.run();
}
