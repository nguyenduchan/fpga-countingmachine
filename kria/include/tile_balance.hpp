#pragma once

#include <opencv2/core.hpp>

#include <cstdint>

// Same arithmetic as the Vitis HLS kernel fpga/hls/tile_brightness.cpp.
// The FPGA image is fixed at 1280x800 with independent 32x32 tiles,
// 16 pixels per cycle. The kernel reports cycles at 100 MHz.
constexpr int kFpgaClockHz = 100000000;

struct TileBalanceStats {
    int tiles_x = 0;
    int tiles_y = 0;
    int tile_width = 0;
    int tile_height = 0;
    std::uint64_t fpga_cycles = 0;
    double fpga_us = 0.0;
    double host_us = 0.0;
};

// Scales every tile so its mean matches the frame mean. gray is CV_8UC1 and is updated in place.
void balance_tiles(cv::Mat& gray, int tile_width, int tile_height, TileBalanceStats& stats);
