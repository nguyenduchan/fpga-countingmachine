// Median of the 3x3 neighborhood. Keeps edges better than a mean blur.

#ifndef KEYENCE_MEDIAN_HPP
#define KEYENCE_MEDIAN_HPP

#include "keyence_scan.hpp"

struct MedianTag {};

inline void keyence_sort9(uint8_t* value) {
    for (int i = 0; i < 9; ++i) {
#pragma HLS UNROLL
        for (int j = i + 1; j < 9; ++j) {
#pragma HLS UNROLL
            if (value[j] < value[i]) {
                const uint8_t swap = value[i];
                value[i] = value[j];
                value[j] = swap;
            }
        }
    }
}

template <>
inline void keyence_emit3<MedianTag>(const uint8_t* row0, const uint8_t* row1, const uint8_t* row2,
                                     uint8_t* dst, int width, int p0, int p1) {
    (void)p0;
    (void)p1;
    const uint8_t* rows[3] = {row0, row1, row2};
    for (int x0 = 0; x0 < width; x0 += 16) {
#pragma HLS PIPELINE II=1
        for (int lane = 0; lane < 16; ++lane) {
#pragma HLS UNROLL
            const int x = x0 + lane;
            if (x >= width) {
                continue;
            }
            uint8_t window[9];
#pragma HLS ARRAY_PARTITION variable=window complete
        int index = 0;
        for (int dy = 0; dy < 3; ++dy) {
#pragma HLS UNROLL
            for (int dx = -1; dx <= 1; ++dx) {
#pragma HLS UNROLL
                window[index] = key_at_x(rows[dy], x + dx, width);
                ++index;
            }
        }
        keyence_sort9(window);
            dst[x] = key_dsp_keep<4>(window[4]);
        }
    }
}

#ifndef __SYNTHESIS__
inline uint8_t median_at(const uint8_t* image, int width, int height, int x, int y) {
    uint8_t window[9];
    int index = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        const int yy = key_clamp_index(y + dy, height);
        for (int dx = -1; dx <= 1; ++dx) {
            const int xx = key_clamp_index(x + dx, width);
            window[index] = image[yy * width + xx];
            ++index;
        }
    }
    keyence_sort9(window);
    return window[4];
}

inline void apply_median(const uint8_t* image, uint8_t* out, int width, int height) {
    keyence_apply3<MedianTag>(image, out, width, height, 0, 0);
}
#endif

#endif
