// Sliding-window brightness. This is the code that moves to the FPGA.
// The laptop test and the Vitis HLS kernel both include this file, so the
// pixels checked on the laptop are the pixels the FPGA writes.
//
// out = pixel * (sigma_target / sigma) + (mu_target - gain * mu)
// mu_target is 128 and sigma_target is 40. Gain is at most 8x.
// A flat neighborhood (sigma 0) is shifted toward 128 and is not amplified.
// The outer 16 pixels reuse the nearest full window.

#ifndef TILE_BRIGHTNESS_CORE_HPP
#define TILE_BRIGHTNESS_CORE_HPP

#include <stdint.h>

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#pragma GCC diagnostic ignored "-Wunused-label"
#endif

static const int kWidth = 1280;
static const int kHeight = 800;
static const int kWindow = 32;
static const int kAreaShift = 10;
static const int kMuTarget = 128;
static const int kSigmaTarget = 40;
static const int kMaxGainQ8 = 8 * 256;

// Shading gain: (128 << 8) / mean, clamped at 8x. Mean 0 uses the clamp.
// Same idea as Keyence shading and Cognex ShadingCorrection: one multiply per pixel.
static const int16_t kShadeGainQ8[256] = {
    2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048,
    2048, 1927, 1820, 1724, 1638, 1560, 1489, 1424, 1365, 1310, 1260, 1213, 1170, 1129, 1092, 1057,
    1024, 992, 963, 936, 910, 885, 862, 840, 819, 799, 780, 762, 744, 728, 712, 697,
    682, 668, 655, 642, 630, 618, 606, 595, 585, 574, 564, 555, 546, 537, 528, 520,
    512, 504, 496, 489, 481, 474, 468, 461, 455, 448, 442, 436, 431, 425, 420, 414,
    409, 404, 399, 394, 390, 385, 381, 376, 372, 368, 364, 360, 356, 352, 348, 344,
    341, 337, 334, 330, 327, 324, 321, 318, 315, 312, 309, 306, 303, 300, 297, 295,
    292, 289, 287, 284, 282, 280, 277, 275, 273, 270, 268, 266, 264, 262, 260, 258,
    256, 254, 252, 250, 248, 246, 244, 242, 240, 239, 237, 235, 234, 232, 230, 229,
    227, 225, 224, 222, 221, 219, 218, 217, 215, 214, 212, 211, 210, 208, 207, 206,
    204, 203, 202, 201, 199, 198, 197, 196, 195, 193, 192, 191, 190, 189, 188, 187,
    186, 185, 184, 183, 182, 181, 180, 179, 178, 177, 176, 175, 174, 173, 172, 171,
    170, 169, 168, 168, 167, 166, 165, 164, 163, 163, 162, 161, 160, 159, 159, 158,
    157, 156, 156, 155, 154, 153, 153, 152, 151, 151, 150, 149, 148, 148, 147, 146,
    146, 145, 144, 144, 143, 143, 142, 141, 141, 140, 140, 139, 138, 138, 137, 137,
    136, 135, 135, 134, 134, 133, 133, 132, 132, 131, 131, 130, 130, 129, 129, 128};

static uint8_t shade_pixel(uint8_t pixel, uint32_t box_sum) {
    uint32_t mean = (box_sum + 512u) >> kAreaShift;
    if (mean > 255u) {
        mean = 255u;
    }
    const int32_t scaled = static_cast<int32_t>(pixel) * static_cast<int32_t>(kShadeGainQ8[mean]);
#pragma HLS BIND_OP variable=scaled op=mul impl=dsp
    const int32_t value = (scaled + 128) >> 8;
    return static_cast<uint8_t>(value > 255 ? 255 : value);
}

// store_rows has room for the center row plus the 15 repeated bottom rows.
static void brightness_plan(int y, int image_height, int store_rows[16], int* store_count, int* save_top) {
    *store_count = 0;
    *save_top = 0;
    if (y < (kWindow - 1)) {
        return;
    }
    const int center_y = y - 15;
    store_rows[(*store_count)++] = center_y;
    if (center_y == 16) {
        *save_top = 1;
    }
    if (center_y == (image_height - 16)) {
        for (int row = image_height - 15; row < image_height; ++row) {
            store_rows[(*store_count)++] = row;
        }
    }
}

static void slide_row(int y, int width, int height, const uint8_t* row_pix, uint8_t hist[kWindow][kWidth],
                      uint16_t hring[kWindow][kWidth], uint32_t* vsum, uint8_t* out_pix) {
#pragma HLS INLINE
    uint32_t run = 0;
    const int slot = y & (kWindow - 1);
columns:
    for (int x0 = 0; x0 < width; x0 += 16) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=vsum inter false
#pragma HLS DEPENDENCE variable=hring inter false
#pragma HLS DEPENDENCE variable=hist inter false
        uint32_t acc = run;
        for (int lane = 0; lane < 16; ++lane) {
#pragma HLS UNROLL
            const int x = x0 + lane;
            if (x < width) {
                const uint8_t pixel = row_pix[x];
                acc += pixel;
                if (x >= kWindow) {
                    acc -= row_pix[x - kWindow];
                }
                const uint16_t horizontal =
                    (x >= (kWindow - 1)) ? static_cast<uint16_t>(acc) : static_cast<uint16_t>(0);
                const uint32_t leaving = (y >= kWindow) ? hring[slot][x] : 0u;
                hring[slot][x] = horizontal;
                hist[slot][x] = pixel;
                if (x >= (kWindow - 1)) {
                    vsum[x] = vsum[x] + horizontal - leaving;
                }
                if (y >= (kWindow - 1) && x >= (kWindow - 1)) {
                    const int center_x = x - 15;
                    const int center_row = y - 15;
                    const uint8_t center = hist[center_row & (kWindow - 1)][center_x];
                    out_pix[center_x] = shade_pixel(center, vsum[x]);
                }
            }
        }
        run = acc;
    }

    if (y >= (kWindow - 1)) {
    fill_left:
        for (int x = 0; x < 16; ++x) {
#pragma HLS PIPELINE II=1
            out_pix[x] = out_pix[16];
        }
    fill_right:
        for (int x = width - 15; x < width; ++x) {
#pragma HLS PIPELINE II=1
            out_pix[x] = out_pix[width - 16];
        }
    }
}

// Same row schedule as the HLS kernel, on a plain grayscale buffer.
static void shade_image(const uint8_t* image_in, uint8_t* image_out, int width, int height) {
    if (width < kWindow || height < kWindow || width > kWidth || height > kHeight) {
        const int count = width * height;
        for (int index = 0; index < count; ++index) {
            image_out[index] = image_in[index];
        }
        return;
    }
    uint8_t hist[kWindow][kWidth];
    uint16_t hring[kWindow][kWidth];
    uint32_t vsum[kWidth];
    uint8_t out_pix[kWidth];
    uint8_t top_pix[kWidth];
    for (int x = 0; x < width; ++x) {
        vsum[x] = 0;
    }
    for (int y = 0; y < height; ++y) {
        slide_row(y, width, height, image_in + y * width, hist, hring, vsum, out_pix);
        int store_rows[16];
        int store_count = 0;
        int save_top = 0;
        brightness_plan(y, height, store_rows, &store_count, &save_top);
        for (int index = 0; index < store_count; ++index) {
            uint8_t* destination = image_out + store_rows[index] * width;
            for (int x = 0; x < width; ++x) {
                destination[x] = out_pix[x];
            }
        }
        if (save_top) {
            for (int x = 0; x < width; ++x) {
                top_pix[x] = out_pix[x];
            }
        }
    }
    for (int row = 0; row < 16; ++row) {
        uint8_t* destination = image_out + row * width;
        for (int x = 0; x < width; ++x) {
            destination[x] = top_pix[x];
        }
    }
}

static void apply_brightness(const uint8_t* image_in, uint8_t* image_out) {
    shade_image(image_in, image_out, kWidth, kHeight);
}

#endif
