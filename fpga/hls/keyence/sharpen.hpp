// Sharpen. 3x3 kernel with 5 at the center. A flat field is unchanged.

#ifndef KEYENCE_SHARPEN_HPP
#define KEYENCE_SHARPEN_HPP

#include "keyence_scan.hpp"

struct SharpenTag {};

template <>
inline void keyence_emit3<SharpenTag>(const uint8_t* row0, const uint8_t* row1, const uint8_t* row2,
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
            const int center = row1[key_clamp_index(x, width)];
            const int value = key_dsp_mul<20>(center, 5) - row0[key_clamp_index(x, width)] -
                          row2[key_clamp_index(x, width)] -
                          key_at_x(row1, x - 1, width) - key_at_x(row1, x + 1, width);
            dst[x] = key_clamp_u8(value);
        }
    }
}

#ifndef __SYNTHESIS__
inline void apply_sharpen(const uint8_t* image, uint8_t* out, int width, int height) {
    keyence_apply3<SharpenTag>(image, out, width, height, 0, 0);
}
#endif

#endif
