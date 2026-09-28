// Preserve intensity. One gain from the reference mean, applied to every pixel.

#ifndef KEYENCE_PRESERVE_INTENSITY_HPP
#define KEYENCE_PRESERVE_INTENSITY_HPP

#include "keyence_scan.hpp"

inline void preserve_sum_row(const uint8_t* row, int width, uint32_t* sum) {
    for (int x0 = 0; x0 < width; x0 += 16) {
#pragma HLS PIPELINE II=1
        int partial = 0;
        for (int lane = 0; lane < 16; ++lane) {
#pragma HLS UNROLL
            const int x = x0 + lane;
            if (x < width) {
                partial += row[x];
            }
        }
        *sum += partial;
    }
}

inline int preserve_gain_q8(uint32_t sum_ref, uint32_t sum_cur) {
    if (sum_cur == 0) {
        return 2048;
    }
    const uint64_t gain = (static_cast<uint64_t>(sum_ref) << 8) / sum_cur;
    if (gain > 2048u) {
        return 2048;
    }
    return static_cast<int>(gain);
}

inline uint8_t preserve_pixel(uint8_t pixel, int gain_q8) {
    const int scaled = key_dsp_mul<42>(pixel, gain_q8);
    return key_clamp_u8((scaled + 128) >> 8);
}

#ifndef __SYNTHESIS__
inline void apply_preserve_intensity(const uint8_t* current, const uint8_t* reference, uint8_t* out,
                                     int width, int height) {
    uint32_t sum_cur = 0;
    uint32_t sum_ref = 0;
    const int count = width * height;
    for (int i = 0; i < count; ++i) {
        sum_cur += current[i];
        sum_ref += reference[i];
    }
    const int gain = preserve_gain_q8(sum_ref, sum_cur);
    for (int i = 0; i < count; ++i) {
        out[i] = preserve_pixel(current[i], gain);
    }
}
#endif

#endif
