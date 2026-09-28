// LumiTrax separation for four directional frames.
// Texture is the mean across directions (the part common to every light).
// Shape is max minus min (the part that moves when the light moves).

#ifndef KEYENCE_LUMITRAX_HPP
#define KEYENCE_LUMITRAX_HPP

#include "keyence_scan.hpp"

static const int kLumiDirections = 4;

inline void lumitrax_row(const uint8_t* dir0, const uint8_t* dir1, const uint8_t* dir2, const uint8_t* dir3,
                         uint8_t* texture, uint8_t* shape, int width) {
    for (int x0 = 0; x0 < width; x0 += 16) {
#pragma HLS PIPELINE II=1
        for (int lane = 0; lane < 16; ++lane) {
#pragma HLS UNROLL
            const int x = x0 + lane;
            if (x >= width) {
                continue;
            }
            const int sample0 = dir0[x];
        const int sample1 = dir1[x];
        const int sample2 = dir2[x];
        const int sample3 = dir3[x];
        int low = sample0;
        int high = sample0;
        if (sample1 < low) {
            low = sample1;
        }
        if (sample1 > high) {
            high = sample1;
        }
        if (sample2 < low) {
            low = sample2;
        }
        if (sample2 > high) {
            high = sample2;
        }
        if (sample3 < low) {
            low = sample3;
        }
        if (sample3 > high) {
            high = sample3;
        }
        const int sum = sample0 + sample1 + sample2 + sample3 + 2;
        texture[x] = static_cast<uint8_t>(key_dsp_mul<30>(sum, 16384) >> 16);
            shape[x] = static_cast<uint8_t>(high - low);
        }
    }
}

#ifndef __SYNTHESIS__
inline void apply_lumitrax(const uint8_t* dir0, const uint8_t* dir1, const uint8_t* dir2, const uint8_t* dir3,
                           uint8_t* texture, uint8_t* shape, int width, int height) {
    if (width <= 0 || height <= 0 || width > kKeyWidth || height > kKeyHeight) {
        return;
    }
    for (int y = 0; y < height; ++y) {
        const int offset = y * width;
        lumitrax_row(dir0 + offset, dir1 + offset, dir2 + offset, dir3 + offset,
                     texture + offset, shape + offset, width);
    }
}
#endif

#endif
