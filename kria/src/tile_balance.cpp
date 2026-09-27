#include "tile_balance.hpp"

#include <algorithm>
#include <chrono>
#include <vector>

namespace {

std::uint64_t core_cycles(const cv::Mat& gray, int tile_width, int tile_height, int tiles_x, int tiles_y) {
    (void)tile_width;
    (void)tile_height;
    const std::uint64_t groups = static_cast<std::uint64_t>(gray.rows) * static_cast<std::uint64_t>(gray.cols / 16);
    const std::uint64_t tiles = static_cast<std::uint64_t>(tiles_x) * static_cast<std::uint64_t>(tiles_y);
    return groups * 2u + tiles;
}

}  // namespace

void balance_tiles(cv::Mat& gray, int tile_width, int tile_height, TileBalanceStats& stats) {
    const auto started = std::chrono::steady_clock::now();
    stats = {};
    stats.tile_width = tile_width;
    stats.tile_height = tile_height;
    if (gray.empty() || gray.type() != CV_8UC1 || tile_width < 1 || tile_height < 1) {
        return;
    }
    if (!gray.isContinuous()) {
        gray = gray.clone();
    }

    const int tiles_x = (gray.cols + tile_width - 1) / tile_width;
    const int tiles_y = (gray.rows + tile_height - 1) / tile_height;
    stats.tiles_x = tiles_x;
    stats.tiles_y = tiles_y;

    std::vector<std::uint32_t> sum(static_cast<std::size_t>(tiles_x * tiles_y), 0);
    std::vector<std::uint32_t> count(sum.size(), 0);
    std::uint64_t total = 0;

    for (int y = 0; y < gray.rows; ++y) {
        const std::uint8_t* row = gray.ptr<std::uint8_t>(y);
        const int tile_y = y / tile_height;
        for (int x = 0; x < gray.cols; ++x) {
            const int tile_x = x / tile_width;
            const std::size_t tile = static_cast<std::size_t>(tile_y * tiles_x + tile_x);
            sum[tile] += row[x];
            count[tile] += 1;
            total += row[x];
        }
    }

    const std::uint32_t pixels = static_cast<std::uint32_t>(gray.cols) * static_cast<std::uint32_t>(gray.rows);
    const std::uint32_t target = pixels == 0 ? 1u : static_cast<std::uint32_t>(total / pixels);
    const std::uint32_t target_safe = target == 0 ? 1u : target;

    for (int y = 0; y < gray.rows; ++y) {
        std::uint8_t* row = gray.ptr<std::uint8_t>(y);
        const int tile_y = y / tile_height;
        for (int x = 0; x < gray.cols; ++x) {
            const int tile_x = x / tile_width;
            const std::size_t tile = static_cast<std::size_t>(tile_y * tiles_x + tile_x);
            const std::uint32_t mean = count[tile] == 0 ? 1u : std::max(1u, sum[tile] / count[tile]);
            const std::uint32_t gain_q8 = (target_safe << 8) / mean;
            const std::uint32_t scaled = (static_cast<std::uint32_t>(row[x]) * gain_q8 + 128u) >> 8;
            row[x] = static_cast<std::uint8_t>(std::min(scaled, 255u));
        }
    }

    stats.fpga_cycles = core_cycles(gray, tile_width, tile_height, tiles_x, tiles_y);
    stats.fpga_us = static_cast<double>(stats.fpga_cycles) * 1.0e6 / static_cast<double>(kFpgaClockHz);
    stats.host_us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count();
}
