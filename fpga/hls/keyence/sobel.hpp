// Sobel. Sum of the absolute X and Y responses, already scaled.

#ifndef KEYENCE_SOBEL_HPP
#define KEYENCE_SOBEL_HPP

#include "sobel_x.hpp"
#include "sobel_y.hpp"

struct SobelTag {};

template <>
inline void keyence_emit3<SobelTag>(const uint8_t* row0, const uint8_t* row1, const uint8_t* row2,
                                    uint8_t* dst, int width, int p0, int p1) {
    (void)p0;
    (void)p1;
    uint8_t gx[kKeyWidth];
    uint8_t gy[kKeyWidth];
    keyence_emit3<SobelXTag>(row0, row1, row2, gx, width, 0, 0);
    keyence_emit3<SobelYTag>(row0, row1, row2, gy, width, 0, 0);
    for (int x0 = 0; x0 < width; x0 += 16) {
#pragma HLS PIPELINE II=1
        for (int lane = 0; lane < 16; ++lane) {
#pragma HLS UNROLL
            const int x = x0 + lane;
            if (x < width) {
                dst[x] = key_clamp_u8(static_cast<int>(gx[x]) + static_cast<int>(gy[x]));
            }
        }
    }
}

#ifndef __SYNTHESIS__
inline void apply_sobel(const uint8_t* image, uint8_t* out, int width, int height) {
    keyence_apply3<SobelTag>(image, out, width, height, 0, 0);
}
#endif

#endif
