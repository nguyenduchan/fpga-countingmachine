// Noise isolation. Small bright or dark clusters are extracted or removed.
// A cluster is a pixel at or above 240, or at or below 16.
// extract = 1 writes only those clusters. extract = 0 paints them with the 3x3 median.

#ifndef KEYENCE_NOISE_ISOLATION_HPP
#define KEYENCE_NOISE_ISOLATION_HPP

#include "blob.hpp"
#include "median.hpp"

static const int kNoiseBright = 240;
static const int kNoiseDark = 16;

inline void noise_threshold_row(const uint8_t* src, uint8_t* mask, int width) {
    for (int x0 = 0; x0 < width; x0 += 16) {
#pragma HLS PIPELINE II=1
        for (int lane = 0; lane < 16; ++lane) {
#pragma HLS UNROLL
            const int x = x0 + lane;
            if (x < width) {
                const int pixel = src[x];
                mask[x] = (pixel >= kNoiseBright || pixel <= kNoiseDark) ? 255 : 0;
            }
        }
    }
}

inline void noise_combine_row(const uint8_t* original, const uint8_t* mask, const uint8_t* median,
                              uint8_t* dst, int width) {
    for (int x0 = 0; x0 < width; x0 += 16) {
#pragma HLS PIPELINE II=1
        for (int lane = 0; lane < 16; ++lane) {
#pragma HLS UNROLL
            const int x = x0 + lane;
            if (x < width) {
                dst[x] = mask[x] ? median[x] : original[x];
            }
        }
    }
}

#ifndef __SYNTHESIS__
inline void apply_noise_isolation(const uint8_t* image, uint8_t* out, uint8_t* scratch, uint16_t* labels,
                                  int width, int height, int max_area, int extract) {
    if (width <= 0 || height <= 0 || width > kKeyWidth || height > kKeyHeight) {
        return;
    }
    for (int y = 0; y < height; ++y) {
        noise_threshold_row(image + y * width, scratch + y * width, width);
    }
    apply_blob(scratch, out, labels, width, height, 1, max_area, 1);
    if (extract) {
        return;
    }
    apply_median(image, scratch, width, height);
    for (int y = 0; y < height; ++y) {
        noise_combine_row(image + y * width, out + y * width, scratch + y * width, out + y * width, width);
    }
}
#endif

#endif
