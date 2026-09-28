// Scratch defect extraction.
// Along a length-9 line, the white top-hat and black top-hat are taken
// horizontally and vertically. The brighter of those four responses is kept.
// A flat field stays 0. A thin line or speck is kept.

#ifndef KEYENCE_SCRATCH_DEFECT_HPP
#define KEYENCE_SCRATCH_DEFECT_HPP

#include "keyence_scan.hpp"

static const int kScratchRadius = 4;
static const int kScratchLines = kScratchRadius * 4 + 1;

inline void scratch_minmax(const uint8_t* src, uint8_t* eroded, uint8_t* dilated, int length, int radius) {
    for (int x0 = 0; x0 < length; x0 += 16) {
#pragma HLS PIPELINE II=1
        for (int lane = 0; lane < 16; ++lane) {
#pragma HLS UNROLL
            const int x = x0 + lane;
            if (x >= length) {
                continue;
            }
            uint8_t low = 255;
        uint8_t high = 0;
        for (int k = -radius; k <= radius; ++k) {
#pragma HLS UNROLL
            const uint8_t sample = key_at_x(src, x + k, length);
            if (sample < low) {
                low = sample;
            }
            if (sample > high) {
                high = sample;
            }
        }
            eroded[x] = low;
            dilated[x] = high;
        }
    }
}

inline int scratch_slot(int row_base, int line) {
    int index = row_base + line;
    if (index >= kScratchLines) {
        index -= kScratchLines;
    }
    return index;
}

inline void scratch_emit(uint8_t rows[kScratchLines][kKeyWidth], int width, uint8_t* dst, int row_base = 0) {
    const uint8_t* center_row = rows[scratch_slot(row_base, kScratchLines / 2)];
    uint8_t eroded_h[kKeyWidth];
    uint8_t dilated_h[kKeyWidth];
    scratch_minmax(center_row, eroded_h, dilated_h, width, kScratchRadius);
    uint8_t opened_h[kKeyWidth];
    uint8_t closed_h[kKeyWidth];
    uint8_t unused[kKeyWidth];
    scratch_minmax(eroded_h, unused, opened_h, width, kScratchRadius);
    scratch_minmax(dilated_h, closed_h, unused, width, kScratchRadius);

    for (int x0 = 0; x0 < width; x0 += 16) {
#pragma HLS PIPELINE II=1
        for (int lane = 0; lane < 16; ++lane) {
#pragma HLS UNROLL
            const int x = x0 + lane;
            if (x >= width) {
                continue;
            }
            uint8_t column[kScratchLines];
#pragma HLS ARRAY_PARTITION variable=column complete
        for (int line = 0; line < kScratchLines; ++line) {
#pragma HLS UNROLL
            column[line] = rows[scratch_slot(row_base, line)][x];
        }
        uint8_t erode[kScratchRadius * 2 + 1];
        uint8_t dilate[kScratchRadius * 2 + 1];
        for (int center = 0; center < kScratchRadius * 2 + 1; ++center) {
#pragma HLS UNROLL
            uint8_t low = 255;
            uint8_t high = 0;
            for (int k = 0; k < kScratchRadius * 2 + 1; ++k) {
#pragma HLS UNROLL
                const uint8_t sample = column[center + k];
                if (sample < low) {
                    low = sample;
                }
                if (sample > high) {
                    high = sample;
                }
            }
            erode[center] = low;
            dilate[center] = high;
        }
        uint8_t open_v = 0;
        uint8_t close_v = 255;
        for (int center = 0; center < kScratchRadius * 2 + 1; ++center) {
#pragma HLS UNROLL
            if (erode[center] > open_v) {
                open_v = erode[center];
            }
            if (dilate[center] < close_v) {
                close_v = dilate[center];
            }
        }
        const int center = rows[scratch_slot(row_base, kScratchLines / 2)][x];
        int bright = center - static_cast<int>(opened_h[x]);
        const int bright_v = center - static_cast<int>(open_v);
        if (bright_v > bright) {
            bright = bright_v;
        }
        int dark = static_cast<int>(closed_h[x]) - center;
        const int dark_v = static_cast<int>(close_v) - center;
        if (dark_v > dark) {
            dark = dark_v;
        }
        if (bright < 0) {
            bright = 0;
        }
        if (dark < 0) {
            dark = 0;
        }
            dst[x] = key_dsp_keep<7>(bright > dark ? bright : dark);
        }
    }
}

#ifndef __SYNTHESIS__
inline void apply_scratch_defect(const uint8_t* image, uint8_t* out, int width, int height) {
    if (width <= 0 || height <= 0 || width > kKeyWidth || height > kKeyHeight) {
        return;
    }
    uint8_t rows[kScratchLines][kKeyWidth];
    uint8_t dst[kKeyWidth];
    for (int y = 0; y < height; ++y) {
        for (int line = 0; line < kScratchLines; ++line) {
            key_copy_row(image, width, height, y - (kScratchLines / 2) + line, rows[line]);
        }
        scratch_emit(rows, width, dst);
        key_store_row(out, width, y, dst);
    }
}
#endif

#endif
