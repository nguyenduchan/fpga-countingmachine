// Subtraction. Absolute difference against a registered master image.

#ifndef KEYENCE_SUBTRACTION_HPP
#define KEYENCE_SUBTRACTION_HPP

#include "keyence_scan.hpp"

struct SubtractTag {};

template <>
inline void keyence_emit2<SubtractTag>(const uint8_t* current, const uint8_t* reference, uint8_t* dst,
                                       int width, int p0, int p1) {
    (void)p0;
    (void)p1;
    for (int x0 = 0; x0 < width; x0 += 16) {
#pragma HLS PIPELINE II=1
        for (int lane = 0; lane < 16; ++lane) {
#pragma HLS UNROLL
            const int x = x0 + lane;
            if (x < width) {
                const int delta = key_abs(static_cast<int>(current[x]) - static_cast<int>(reference[x]));
                dst[x] = key_dsp_keep<5>(delta);
            }
        }
    }
}

#ifndef __SYNTHESIS__
inline void apply_subtraction(const uint8_t* current, const uint8_t* reference, uint8_t* out,
                              int width, int height) {
    keyence_apply2<SubtractTag>(current, reference, out, width, height, 0, 0);
}
#endif

#endif
