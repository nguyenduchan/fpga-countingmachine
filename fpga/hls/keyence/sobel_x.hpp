// Sobel X. Horizontal shading change, scaled by 4 into 8 bits.

#ifndef KEYENCE_SOBEL_X_HPP
#define KEYENCE_SOBEL_X_HPP

#include "keyence_scan.hpp"

struct SobelXTag {};

template <>
inline void keyence_emit3<SobelXTag>(const uint8_t* row0, const uint8_t* row1, const uint8_t* row2,
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
            const int gx = -key_at_x(row0, x - 1, width) + key_at_x(row0, x + 1, width) -
                       key_dsp_mul<10>(key_at_x(row1, x - 1, width), 2) +
                       key_dsp_mul<11>(key_at_x(row1, x + 1, width), 2) -
                       key_at_x(row2, x - 1, width) + key_at_x(row2, x + 1, width);
            dst[x] = key_clamp_u8(key_abs(gx) >> 2);
        }
    }
}

#ifndef __SYNTHESIS__
inline void apply_sobel_x(const uint8_t* image, uint8_t* out, int width, int height) {
    keyence_apply3<SobelXTag>(image, out, width, height, 0, 0);
}
#endif

#endif
