// Expand: grayscale dilation. A dark spike takes the brightest neighbor.

#ifndef KEYENCE_EXPAND_HPP
#define KEYENCE_EXPAND_HPP

#include "keyence_scan.hpp"

struct ExpandTag {};

template <>
inline void keyence_emit3<ExpandTag>(const uint8_t* row0, const uint8_t* row1, const uint8_t* row2,
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
            uint8_t value = 0;
        for (int dy = 0; dy < 3; ++dy) {
#pragma HLS UNROLL
            const uint8_t* row = dy == 0 ? row0 : (dy == 1 ? row1 : row2);
            for (int dx = -1; dx <= 1; ++dx) {
#pragma HLS UNROLL
                const uint8_t sample = key_at_x(row, x + dx, width);
                if (sample > value) {
                    value = sample;
                }
            }
        }
            dst[x] = key_dsp_keep<2>(value);
        }
    }
}

#ifndef __SYNTHESIS__
inline void apply_expand(const uint8_t* image, uint8_t* out, int width, int height) {
    keyence_apply3<ExpandTag>(image, out, width, height, 0, 0);
}
#endif

#endif
