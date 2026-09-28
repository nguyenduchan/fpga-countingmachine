// Laplacian. Direction-free edge, written as an absolute response.

#ifndef KEYENCE_LAPLACIAN_HPP
#define KEYENCE_LAPLACIAN_HPP

#include "keyence_scan.hpp"

struct LaplacianTag {};

template <>
inline void keyence_emit3<LaplacianTag>(const uint8_t* row0, const uint8_t* row1, const uint8_t* row2,
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
            const int lap = key_dsp_mul<21>(center, 4) - row0[key_clamp_index(x, width)] -
                        row2[key_clamp_index(x, width)] -
                        key_at_x(row1, x - 1, width) - key_at_x(row1, x + 1, width);
            dst[x] = key_clamp_u8(key_abs(lap));
        }
    }
}

#ifndef __SYNTHESIS__
inline void apply_laplacian(const uint8_t* image, uint8_t* out, int width, int height) {
    keyence_apply3<LaplacianTag>(image, out, width, height, 0, 0);
}
#endif

#endif
