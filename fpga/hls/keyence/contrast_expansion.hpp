// Contrast expansion. Stretch the histogram, then blend by expansion (0..256).

#ifndef KEYENCE_CONTRAST_EXPANSION_HPP
#define KEYENCE_CONTRAST_EXPANSION_HPP

#include "keyence_scan.hpp"

inline void contrast_hist_row(const uint8_t* row, int width, int hist[256]) {
    for (int x = 0; x < width; ++x) {
#pragma HLS PIPELINE II=1
        hist[row[x]] += 1;
    }
}

inline void contrast_limits(const int hist[256], int noise_cut, int* lo, int* hi) {
    int seen = 0;
    int lo_v = 0;
    int hi_v = 255;
    for (int value = 0; value < 256; ++value) {
        seen += hist[value];
        if (seen > noise_cut) {
            lo_v = value;
            break;
        }
    }
    seen = 0;
    for (int value = 255; value >= 0; --value) {
        seen += hist[value];
        if (seen > noise_cut) {
            hi_v = value;
            break;
        }
    }
    if (hi_v <= lo_v) {
        hi_v = lo_v + 1;
    }
    *lo = lo_v;
    *hi = hi_v;
}

inline uint8_t contrast_map_pixel(uint8_t pixel, int lo, int hi, int expansion) {
    const int span = hi - lo;
    const int stretched = key_dsp_mul<50>(static_cast<int>(pixel) - lo, 255) / span;
    const int clamped = stretched < 0 ? 0 : (stretched > 255 ? 255 : stretched);
    const int blended = key_dsp_mul<51>(pixel, 256 - expansion) + key_dsp_mul<52>(clamped, expansion);
    return key_clamp_u8(blended >> 8);
}

#ifndef __SYNTHESIS__
inline void apply_contrast_expansion(const uint8_t* image, uint8_t* out, int width, int height,
                                     int expansion, int noise_cut) {
    int hist[256];
    for (int bin = 0; bin < 256; ++bin) {
        hist[bin] = 0;
    }
    for (int y = 0; y < height; ++y) {
        contrast_hist_row(image + y * width, width, hist);
    }
    int lo = 0;
    int hi = 255;
    contrast_limits(hist, noise_cut, &lo, &hi);
    const int count = width * height;
    for (int i = 0; i < count; ++i) {
        out[i] = contrast_map_pixel(image[i], lo, hi, expansion);
    }
}
#endif

#endif
