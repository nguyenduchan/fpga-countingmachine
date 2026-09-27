#pragma once

#include <iostream>
#include <string>

inline void log_line(const std::string& message) {
    std::cerr << "[kria_eth_camera] " << message << std::endl;
}
