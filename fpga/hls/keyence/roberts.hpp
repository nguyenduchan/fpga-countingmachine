// Roberts. Diagonal pair on the current pixel and its lower-right neighbors.

#ifndef KEYENCE_ROBERTS_HPP
#define KEYENCE_ROBERTS_HPP

#include "keyence_scan.hpp"

struct RobertsTag {};

template <>
inline void keyence_emit3<RobertsTag>(const uint8_t* row0, const uint8_t* row1, const uint8_t* row2,
                                      uint8_t* dst, int width, int p0, int p1) {
    (void)row0;
    (void)p0;
    (void)p1;
    for (int x0 = 0; x0 < width; x0 += 16) {
#pragma HLS PIPELINE II=1
        for (int lane = 0; lane < 16; ++lane) {
#pragma HLS UNROLL
            const int x = x0 + lane;
            if (x >= width) {
                continue;
            }
            const int gx = static_cast<int>(key_at_x(row1, x, width)) - static_cast<int>(key_at_x(row2, x + 1, width));
        const int gy = static_cast<int>(key_at_x(row1, x + 1, width)) - static_cast<int>(key_at_x(row2, x, width));
            dst[x] = key_dsp_keep<6>(key_abs(gx) + key_abs(gy));
        }
    }
}

#ifndef __SYNTHESIS__
inline void apply_roberts(const uint8_t* image, uint8_t* out, int width, int height) {
    keyence_apply3<RobertsTag>(image, out, width, height, 0, 0);
}
#endif

#endif
