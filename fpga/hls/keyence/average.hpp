// Average of the 3x3 neighborhood. Division by 9 is a DSP reciprocal.

#ifndef KEYENCE_AVERAGE_HPP
#define KEYENCE_AVERAGE_HPP

#include "keyence_scan.hpp"

struct AverageTag {};

inline uint8_t keyence_mean3(const uint8_t* row0, const uint8_t* row1, const uint8_t* row2, int x, int width) {
    int sum = 0;
    const uint8_t* rows[3] = {row0, row1, row2};
    for (int dy = 0; dy < 3; ++dy) {
#pragma HLS UNROLL
        for (int dx = -1; dx <= 1; ++dx) {
#pragma HLS UNROLL
            sum += key_at_x(rows[dy], x + dx, width);
        }
    }
    const int scaled = key_dsp_mul<40>(sum, 7282) + 32768;
    return key_clamp_u8(scaled >> 16);
}

template <>
inline void keyence_emit3<AverageTag>(const uint8_t* row0, const uint8_t* row1, const uint8_t* row2,
                                      uint8_t* dst, int width, int p0, int p1) {
    (void)p0;
    (void)p1;
    for (int x0 = 0; x0 < width; x0 += 16) {
#pragma HLS PIPELINE II=1
        for (int lane = 0; lane < 16; ++lane) {
#pragma HLS UNROLL
            const int x = x0 + lane;
            if (x < width) {
                dst[x] = keyence_mean3(row0, row1, row2, x, width);
            }
        }
    }
}

#ifndef __SYNTHESIS__
inline void apply_average(const uint8_t* image, uint8_t* out, int width, int height) {
    keyence_apply3<AverageTag>(image, out, width, height, 0, 0);
}
#endif

#endif
