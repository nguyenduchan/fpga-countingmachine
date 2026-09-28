// Prewitt. X and Y shading summed, then divided by 3 to fit 8 bits.

#ifndef KEYENCE_PREWITT_HPP
#define KEYENCE_PREWITT_HPP

#include "keyence_scan.hpp"

struct PrewittTag {};

template <>
inline void keyence_emit3<PrewittTag>(const uint8_t* row0, const uint8_t* row1, const uint8_t* row2,
                                      uint8_t* dst, int width, int p0, int p1) {
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
            const int left = key_at_x(row0, x - 1, width) + key_at_x(row1, x - 1, width) + key_at_x(row2, x - 1, width);
        const int right = key_at_x(row0, x + 1, width) + key_at_x(row1, x + 1, width) + key_at_x(row2, x + 1, width);
        const int up = key_at_x(row0, x - 1, width) + row0[key_clamp_index(x, width)] + key_at_x(row0, x + 1, width);
        const int down = key_at_x(row2, x - 1, width) + row2[key_clamp_index(x, width)] + key_at_x(row2, x + 1, width);
        const int magnitude = key_abs(right - left) + key_abs(down - up);
            dst[x] = key_clamp_u8(key_dsp_mul<22>(magnitude, 1) / 3);
        }
    }
}

#ifndef __SYNTHESIS__
inline void apply_prewitt(const uint8_t* image, uint8_t* out, int width, int height) {
    keyence_apply3<PrewittTag>(image, out, width, height, 0, 0);
}
#endif

#endif
