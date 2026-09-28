// Image extraction. Subtract a 3x3 mean so a flat background sits at 128.

#ifndef KEYENCE_IMAGE_EXTRACTION_HPP
#define KEYENCE_IMAGE_EXTRACTION_HPP

#include "average.hpp"

struct ExtractTag {};

template <>
inline void keyence_emit2<ExtractTag>(const uint8_t* current, const uint8_t* blurred, uint8_t* dst,
                                      int width, int p0, int p1) {
    (void)p0;
    (void)p1;
    for (int x0 = 0; x0 < width; x0 += 16) {
#pragma HLS PIPELINE II=1
        for (int lane = 0; lane < 16; ++lane) {
#pragma HLS UNROLL
            const int x = x0 + lane;
            if (x < width) {
                dst[x] = key_clamp_u8(static_cast<int>(current[x]) - static_cast<int>(blurred[x]) + 128);
            }
        }
    }
}

#ifndef __SYNTHESIS__
inline void apply_image_extraction(const uint8_t* image, uint8_t* out, uint8_t* scratch,
                                   int width, int height) {
    apply_average(image, scratch, width, height);
    keyence_apply2<ExtractTag>(image, scratch, out, width, height, 0, 0);
}
#endif

#endif
