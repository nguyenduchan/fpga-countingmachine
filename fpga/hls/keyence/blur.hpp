// Blur. direction 0 is the 3x3 mean, 1 is horizontal, 2 is vertical.

#ifndef KEYENCE_BLUR_HPP
#define KEYENCE_BLUR_HPP

#include "keyence_scan.hpp"

struct BlurTag {};

template <>
inline void keyence_emit3<BlurTag>(const uint8_t* row0, const uint8_t* row1, const uint8_t* row2,
                                   uint8_t* dst, int width, int direction, int p1) {
    (void)p1;
    for (int x0 = 0; x0 < width; x0 += 16) {
#pragma HLS PIPELINE II=1
        for (int lane = 0; lane < 16; ++lane) {
#pragma HLS UNROLL
            const int x = x0 + lane;
            if (x >= width) {
                continue;
            }
            int sum = 0;
        int count = 0;
        if (direction == 1) {
            for (int dx = -1; dx <= 1; ++dx) {
#pragma HLS UNROLL
                sum += key_at_x(row1, x + dx, width);
            }
            count = 3;
        } else if (direction == 2) {
            sum = static_cast<int>(row0[key_clamp_index(x, width)]) + row1[key_clamp_index(x, width)] +
                  row2[key_clamp_index(x, width)];
            count = 3;
        } else {
            const uint8_t* rows[3] = {row0, row1, row2};
            for (int dy = 0; dy < 3; ++dy) {
#pragma HLS UNROLL
                for (int dx = -1; dx <= 1; ++dx) {
#pragma HLS UNROLL
                    sum += key_at_x(rows[dy], x + dx, width);
                }
            }
            count = 9;
        }
        const int reciprocal = (count == 9) ? 7282 : 21846;
        const int shift = 16;
        const int scaled = key_dsp_mul<43>(sum, reciprocal) + 32768;
        (void)shift;
            dst[x] = key_clamp_u8(scaled >> 16);
        }
    }
}

#ifndef __SYNTHESIS__
inline void apply_blur(const uint8_t* image, uint8_t* out, int width, int height, int direction) {
    keyence_apply3<BlurTag>(image, out, width, height, direction, 0);
}
#endif

#endif
