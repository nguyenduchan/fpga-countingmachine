// Contrast conversion. offset shifts brightness, span is Q8 (256 = unchanged).

#ifndef KEYENCE_CONTRAST_CONVERSION_HPP
#define KEYENCE_CONTRAST_CONVERSION_HPP

#include "keyence_scan.hpp"

struct ContrastTag {};

template <>
inline void keyence_emit1<ContrastTag>(const uint8_t* src, uint8_t* dst, int width, int offset, int span) {
    for (int x0 = 0; x0 < width; x0 += 16) {
#pragma HLS PIPELINE II=1
        for (int lane = 0; lane < 16; ++lane) {
#pragma HLS UNROLL
            const int x = x0 + lane;
            if (x < width) {
                const int centered = static_cast<int>(src[x]) - 128;
                const int scaled = key_dsp_mul<41>(centered, span);
                dst[x] = key_clamp_u8((scaled >> 8) + 128 + offset);
            }
        }
    }
}

#ifndef __SYNTHESIS__
inline void apply_contrast(const uint8_t* image, uint8_t* out, int width, int height, int offset, int span) {
    keyence_apply1<ContrastTag>(image, out, width, height, offset, span);
}
#endif

#endif
