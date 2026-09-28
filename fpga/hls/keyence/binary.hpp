// Binary. Pixels with brightness between lo and up become white.

#ifndef KEYENCE_BINARY_HPP
#define KEYENCE_BINARY_HPP

#include "keyence_scan.hpp"

struct BinaryTag {};

template <>
inline void keyence_emit1<BinaryTag>(const uint8_t* src, uint8_t* dst, int width, int lo, int up) {
    for (int x0 = 0; x0 < width; x0 += 16) {
#pragma HLS PIPELINE II=1
        for (int lane = 0; lane < 16; ++lane) {
#pragma HLS UNROLL
            const int x = x0 + lane;
            if (x < width) {
                const int pixel = src[x];
                const int bit = (pixel >= lo && pixel <= up) ? 255 : 0;
                dst[x] = key_dsp_keep<1>(bit);
            }
        }
    }
}

#ifndef __SYNTHESIS__
inline void apply_binary(const uint8_t* image, uint8_t* out, int width, int height, int lo, int up) {
    keyence_apply1<BinaryTag>(image, out, width, height, lo, up);
}
#endif

#endif
