// Same chamfer math as fpga/hls/chamfer/chamfer_core.hpp.
//
//   g++ -O2 -std=c++17 -Wno-unknown-pragmas chamfer_match.cpp -o chamfer_match
//   chamfer_match

#include "../../fpga/hls/chamfer/chamfer_core.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool expect(bool ok, const std::string& name) {
    std::cout << (ok ? "PASS " : "FAIL ") << name << "\n";
    return ok;
}

bool test_score_and_distance() {
    constexpr int w = 32;
    constexpr int h = 32;
    std::vector<std::uint8_t> edge(w * h, 0);
    edge[16 * w + 16] = 1;
    std::vector<std::uint16_t> dt(w * h, 0);
    chamfer_from_edges(edge.data(), dt.data(), w, h);
    const bool edge_zero = dt[16 * w + 16] == 0;
    const bool step_right = dt[16 * w + 17] == 3;
    const bool diag = dt[17 * w + 17] == 4;
    const bool two_steps = dt[16 * w + 18] == 6;

    int16_t rdx[1] = {0};
    int16_t rdy[1] = {0};
    const int on_edge = chamfer_score_q8(dt.data(), w, h, 16, 16, rdx, rdy, 1);
    const int beside = chamfer_score_q8(dt.data(), w, h, 17, 16, rdx, rdy, 1);
    bool ok = true;
    ok = expect(edge_zero, "distance on the edge is 0") && ok;
    ok = expect(step_right, "one step east is 3") && ok;
    ok = expect(diag, "one step southeast is 4") && ok;
    ok = expect(two_steps, "two steps east is 6") && ok;
    ok = expect(on_edge == 256, "score on the edge is 1.0 in Q8") && ok;
    ok = expect(beside == 64, "score one pixel off is 256/(1+3)") && ok;

    std::fill(edge.begin(), edge.end(), 0);
    for (int y = 4; y < 28; ++y) {
        edge[y * w + 16] = 1;
    }
    chamfer_from_edges(edge.data(), dt.data(), w, h);
    int16_t model_x[8];
    int16_t model_y[8];
    for (int i = 0; i < 8; ++i) {
        model_x[i] = 0;
        model_y[i] = static_cast<int16_t>(i - 3);
    }
    ChamferPeak peaks[8];
    const int found = chamfer_match(dt.data(), w, h, model_x, model_y, 8, 4, 4, 200, 2, 6, 2, 4, peaks);
    ok = expect(found >= 1 && peaks[0].x == 16 && peaks[0].score_q8 >= 200, "vertical edge model locks onto the line") &&
         ok;
    return ok;
}

void bench() {
    constexpr int w = kChamferWidth;
    constexpr int h = kChamferHeight;
    std::vector<std::uint8_t> gray(w * h, 30);
    for (int y = 96; y < 700; y += 80) {
        for (int x = 80; x < 1200; x += 96) {
            for (int k = -12; k <= 12; ++k) {
                gray[(y + k) * w + x] = 220;
                gray[y * w + (x + k)] = 220;
            }
        }
    }
    int16_t rdx[64];
    int16_t rdy[64];
    int n = 0;
    for (int k = -12; k <= 12 && n < 64; k += 2) {
        rdx[n] = 0;
        rdy[n] = static_cast<int16_t>(k);
        n += 1;
        rdx[n] = static_cast<int16_t>(k);
        rdy[n] = 0;
        n += 1;
    }
    std::vector<std::uint16_t> dt(w * h);
    const auto t0 = std::chrono::steady_clock::now();
    chamfer_distance(gray.data(), dt.data(), w, h, 50);
    const auto t1 = std::chrono::steady_clock::now();
    int found = 0;
    ChamferPeak peaks[16];
    for (int repeat = 0; repeat < 20; ++repeat) {
        found = chamfer_match(dt.data(), w, h, rdx, rdy, n, 16, 16, 180, 8, 24, 2, 16, peaks);
    }
    const auto t2 = std::chrono::steady_clock::now();
    const double dt_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double match_ms = std::chrono::duration<double, std::milli>(t2 - t1).count() / 20.0;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "CPU 1280x800 distance " << dt_ms << " ms, stride-8 match " << match_ms
              << " ms, peaks " << found << " best " << (found ? peaks[0].score_q8 : 0) << "\n";
}

}  // namespace

int main() {
    if (!test_score_and_distance()) {
        std::cerr << "chamfer self-test failed\n";
        return 1;
    }
    bench();
    std::cout << "chamfer self-test passed\n";
    return 0;
}
