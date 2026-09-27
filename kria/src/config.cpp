#include "config.hpp"

#include <cctype>
#include <fstream>
#include <stdexcept>

namespace {

std::string trim(const std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(begin, end - begin);
}

bool parse_int(const std::string& text, int& value) {
    try {
        std::size_t used = 0;
        const int parsed = std::stoi(text, &used);
        if (used != text.size()) {
            return false;
        }
        value = parsed;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

}  // namespace

bool AppConfig::load_file(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        return false;
    }

    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const auto split = line.find('=');
        if (split == std::string::npos) {
            continue;
        }
        const std::string key = trim(line.substr(0, split));
        const std::string value = trim(line.substr(split + 1));
        int number = 0;
        if (key == "board_name") {
            board_name = value;
        } else if (key == "ubuntu_version") {
            ubuntu_version = value;
        } else if (key == "camera_model") {
            camera_model = value;
        } else if (key == "video_device") {
            video_device = value;
        } else if (key == "bind_address") {
            bind_address = value;
        } else if (key == "width" && parse_int(value, number)) {
            width = number;
        } else if (key == "height" && parse_int(value, number)) {
            height = number;
        } else if (key == "fps" && parse_int(value, number)) {
            fps = number;
        } else if (key == "port" && parse_int(value, number)) {
            port = number;
        } else if (key == "jpeg_quality" && parse_int(value, number)) {
            jpeg_quality = number;
        } else if (key == "tile_width" && parse_int(value, number)) {
            tile_width = number;
        } else if (key == "tile_height" && parse_int(value, number)) {
            tile_height = number;
        }
    }
    sanitize();
    return true;
}

void AppConfig::sanitize() {
    if (board_name.empty()) {
        board_name = "Kria KV260";
    }
    if (ubuntu_version.empty()) {
        ubuntu_version = "24.04";
    }
    if (camera_model.empty()) {
        camera_model = "OV9281";
    }
    if (video_device.empty()) {
        video_device = "/dev/video0";
    }
    if (bind_address.empty()) {
        bind_address = "0.0.0.0";
    }
    if (width < 16) {
        width = 1280;
    }
    if (height < 16) {
        height = 800;
    }
    if (fps < 1) {
        fps = 120;
    } else if (fps > 1000) {
        fps = 1000;
    }
    if (port < 1 || port > 65535) {
        port = 5600;
    }
    if (jpeg_quality < 1) {
        jpeg_quality = 1;
    } else if (jpeg_quality > 100) {
        jpeg_quality = 100;
    }
    if (tile_width < 8) {
        tile_width = 8;
    } else if (tile_width > 128) {
        tile_width = 128;
    }
    if (tile_height < 8) {
        tile_height = 8;
    } else if (tile_height > 128) {
        tile_height = 128;
    }
}
