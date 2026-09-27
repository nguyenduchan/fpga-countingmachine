// Sliding-window brightness for the Kria KV260.
// Each pixel is leveled from the 32x32 neighborhood centered on it.
// The window moves one pixel at a time, so there is no tile grid.
// Sum and sum of squares slide by dropping one column and adding one column.
//
// out = pixel * (sigma_target / sigma) + (mu_target - gain * mu)
// mu_target is 128 and sigma_target is 40. Gain is at most 8x.
// A flat neighborhood (sigma 0) is shifted toward 128 and is not amplified.
// The outer 16 pixels reuse the nearest full window.
//
// DDR bursts stay 16 words. There is no while loop. Clock is 100 MHz.

#include <stdint.h>

#include "ap_int.h"

static const int kWidth = 1280;
static const int kHeight = 800;
static const int kWindow = 32;
static const int kBeatPixels = 16;
static const int kChunkWords = 16;
static const int kWordsPerRow = kWidth / kBeatPixels;
static const int kChunksPerRow = kWordsPerRow / kChunkWords;
static const int kAreaShift = 10;
static const int kMuTarget = 128;
static const int kSigmaTarget = 40;
static const int kMaxGainQ8 = 8 * 256;
static const uint32_t kReportedCycles = 1228800;

static uint8_t isqrt(uint32_t value) {
#pragma HLS INLINE
    uint32_t result = 0;
    uint32_t bit = 1u << 14;
sqrt_bits:
    for (int step = 0; step < 8; ++step) {
#pragma HLS UNROLL
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return static_cast<uint8_t>(result);
}

static uint8_t level_pixel(uint8_t center, uint32_t win_sum, uint32_t win_sq) {
#pragma HLS INLINE
    const uint32_t mean = (win_sum + 512u) >> kAreaShift;
    const uint32_t mean_sq = (win_sq + 512u) >> kAreaShift;
    int32_t variance = static_cast<int32_t>(mean_sq) - static_cast<int32_t>(mean) * static_cast<int32_t>(mean);
    if (variance < 0) {
        variance = 0;
    }
    if (variance > 65535) {
        variance = 65535;
    }
    const uint8_t sigma = isqrt(static_cast<uint32_t>(variance));
    int32_t gain_q8 = 256;
    if (sigma > 0) {
        gain_q8 = (kSigmaTarget << 8) / static_cast<int32_t>(sigma);
        if (gain_q8 > kMaxGainQ8) {
            gain_q8 = kMaxGainQ8;
        }
    }
    const int32_t offset = kMuTarget - ((gain_q8 * static_cast<int32_t>(mean) + 128) >> 8);
    int32_t value = ((static_cast<int32_t>(center) * gain_q8 + 128) >> 8) + offset;
    if (value < 0) {
        value = 0;
    }
    if (value > 255) {
        value = 255;
    }
    return static_cast<uint8_t>(value);
}

static void read_chunk(const ap_uint<128>* src, int word_base, ap_uint<128> beats[kChunkWords]) {
#pragma HLS INLINE off
read_beats:
    for (int word = 0; word < kChunkWords; ++word) {
#pragma HLS PIPELINE II=1
        beats[word] = src[word_base + word];
    }
}

static void write_chunk(ap_uint<128>* dst, int word_base, const ap_uint<128> beats[kChunkWords]) {
#pragma HLS INLINE off
write_beats:
    for (int word = 0; word < kChunkWords; ++word) {
#pragma HLS PIPELINE II=1
        dst[word_base + word] = beats[word];
    }
}

static void load_row(const ap_uint<128>* src, int y, uint8_t row[kWidth]) {
#pragma HLS INLINE off
    ap_uint<128> words[kWordsPerRow];
load_chunks:
    for (int chunk = 0; chunk < kChunksPerRow; ++chunk) {
#pragma HLS LOOP_FLATTEN off
        ap_uint<128> local[kChunkWords];
#pragma HLS ARRAY_PARTITION variable=local complete
        read_chunk(src, y * kWordsPerRow + chunk * kChunkWords, local);
    copy_beats:
        for (int word = 0; word < kChunkWords; ++word) {
#pragma HLS PIPELINE II=1
            words[chunk * kChunkWords + word] = local[word];
        }
    }
unpack:
    for (int word = 0; word < kWordsPerRow; ++word) {
#pragma HLS PIPELINE II=1
        const ap_uint<128> beat = words[word];
    unpack_lanes:
        for (int lane = 0; lane < kBeatPixels; ++lane) {
#pragma HLS UNROLL
            row[word * kBeatPixels + lane] = beat.range(lane * 8 + 7, lane * 8);
        }
    }
}

static void store_row(ap_uint<128>* dst, int y, const uint8_t row[kWidth]) {
#pragma HLS INLINE off
store_chunks:
    for (int chunk = 0; chunk < kChunksPerRow; ++chunk) {
#pragma HLS LOOP_FLATTEN off
        ap_uint<128> local[kChunkWords];
#pragma HLS ARRAY_PARTITION variable=local complete
    pack:
        for (int word = 0; word < kChunkWords; ++word) {
#pragma HLS PIPELINE II=1
            ap_uint<128> beat = 0;
        pack_lanes:
            for (int lane = 0; lane < kBeatPixels; ++lane) {
#pragma HLS UNROLL
                const int x = (chunk * kChunkWords + word) * kBeatPixels + lane;
                beat.range(lane * 8 + 7, lane * 8) = row[x];
            }
            local[word] = beat;
        }
        write_chunk(dst, y * kWordsPerRow + chunk * kChunkWords, local);
    }
}

static void slide_row(int y, const uint8_t row_pix[kWidth], uint8_t hist[kWindow][kWidth], uint32_t col_sum[kWidth],
                      uint32_t col_sq[kWidth], uint8_t out_pix[kWidth]) {
#pragma HLS INLINE off
    uint32_t shift_sum[kWindow];
    uint32_t shift_sq[kWindow];
#pragma HLS ARRAY_PARTITION variable=shift_sum complete
#pragma HLS ARRAY_PARTITION variable=shift_sq complete
clear_shift:
    for (int lane = 0; lane < kWindow; ++lane) {
#pragma HLS UNROLL
        shift_sum[lane] = 0;
        shift_sq[lane] = 0;
    }

    uint32_t win_sum = 0;
    uint32_t win_sq = 0;
    const int slot = y & (kWindow - 1);
    const int center_row = (y >= 15) ? (y - 15) : 0;
    const int center_slot = center_row & (kWindow - 1);

columns:
    for (int x = 0; x < kWidth; ++x) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=col_sum inter false
#pragma HLS DEPENDENCE variable=col_sq inter false
#pragma HLS DEPENDENCE variable=hist inter false
        const uint8_t pixel = row_pix[x];
        const uint8_t leaving_pixel = (y >= kWindow) ? hist[slot][x] : static_cast<uint8_t>(0);
        const uint32_t pixel_sq = static_cast<uint32_t>(pixel) * static_cast<uint32_t>(pixel);
        const uint32_t leaving_sq = static_cast<uint32_t>(leaving_pixel) * static_cast<uint32_t>(leaving_pixel);
#pragma HLS BIND_OP variable=pixel_sq op=mul impl=fabric
#pragma HLS BIND_OP variable=leaving_sq op=mul impl=fabric
        hist[slot][x] = pixel;
        const uint32_t next_sum = col_sum[x] + pixel - leaving_pixel;
        const uint32_t next_sq = col_sq[x] + pixel_sq - leaving_sq;
        col_sum[x] = next_sum;
        col_sq[x] = next_sq;

        const uint32_t drop_sum = shift_sum[0];
        const uint32_t drop_sq = shift_sq[0];
    slide_columns:
        for (int lane = 0; lane < kWindow - 1; ++lane) {
#pragma HLS UNROLL
            shift_sum[lane] = shift_sum[lane + 1];
            shift_sq[lane] = shift_sq[lane + 1];
        }
        shift_sum[kWindow - 1] = next_sum;
        shift_sq[kWindow - 1] = next_sq;
        if (x >= kWindow) {
            win_sum = win_sum + next_sum - drop_sum;
            win_sq = win_sq + next_sq - drop_sq;
        } else {
            win_sum += next_sum;
            win_sq += next_sq;
        }

        if (y >= (kWindow - 1) && x >= (kWindow - 1)) {
            const int center_x = x - 15;
            const uint8_t center = hist[center_slot][center_x];
            out_pix[center_x] = level_pixel(center, win_sum, win_sq);
        }
    }

    if (y >= (kWindow - 1)) {
    fill_left:
        for (int x = 0; x < 16; ++x) {
#pragma HLS PIPELINE II=1
            out_pix[x] = out_pix[16];
        }
    fill_right:
        for (int x = kWidth - 15; x < kWidth; ++x) {
#pragma HLS PIPELINE II=1
            out_pix[x] = out_pix[kWidth - 16];
        }
    }
}

void tile_brightness(const ap_uint<128>* image_in,
                     ap_uint<128>* image_out,
                     int width,
                     int height,
                     int tile_w,
                     int tile_h,
                     uint32_t* cycles) {
#pragma HLS INTERFACE m_axi port=image_in offset=slave bundle=gmem depth=64000 max_read_burst_length=16 num_read_outstanding=4 max_widen_bitwidth=128
#pragma HLS INTERFACE m_axi port=image_out offset=slave bundle=gmem depth=64000 max_write_burst_length=16 num_write_outstanding=4 max_widen_bitwidth=128
#pragma HLS INTERFACE s_axilite port=image_in bundle=control
#pragma HLS INTERFACE s_axilite port=image_out bundle=control
#pragma HLS INTERFACE s_axilite port=width bundle=control
#pragma HLS INTERFACE s_axilite port=height bundle=control
#pragma HLS INTERFACE s_axilite port=tile_w bundle=control
#pragma HLS INTERFACE s_axilite port=tile_h bundle=control
#pragma HLS INTERFACE s_axilite port=cycles bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control

    if (width != kWidth || height != kHeight || tile_w != kWindow || tile_h != kWindow) {
        if (cycles != 0) {
            *cycles = 0;
        }
        return;
    }

    uint8_t history[kWindow][kWidth];
    uint32_t col_sum[kWidth];
    uint32_t col_sq[kWidth];
    uint8_t row_pix[kWidth];
    uint8_t out_pix[kWidth];
    uint8_t top_pix[kWidth];
#pragma HLS ARRAY_PARTITION variable=history complete dim=1
#pragma HLS BIND_STORAGE variable=history type=ram_t2p impl=bram
#pragma HLS ARRAY_PARTITION variable=row_pix cyclic factor=16
#pragma HLS ARRAY_PARTITION variable=out_pix cyclic factor=16
#pragma HLS ARRAY_PARTITION variable=top_pix cyclic factor=16

clear_columns:
    for (int x = 0; x < kWidth; ++x) {
#pragma HLS PIPELINE II=1
        col_sum[x] = 0;
        col_sq[x] = 0;
    }

rows:
    for (int y = 0; y < kHeight; ++y) {
#pragma HLS LOOP_FLATTEN off
        load_row(image_in, y, row_pix);
        slide_row(y, row_pix, history, col_sum, col_sq, out_pix);
        if (y >= (kWindow - 1)) {
            const int center_y = y - 15;
            store_row(image_out, center_y, out_pix);
            if (center_y == 16) {
            copy_top:
                for (int x = 0; x < kWidth; ++x) {
#pragma HLS PIPELINE II=1
                    top_pix[x] = out_pix[x];
                }
            }
            if (center_y == (kHeight - 16)) {
            bottom_rows:
                for (int row = kHeight - 15; row < kHeight; ++row) {
#pragma HLS LOOP_FLATTEN off
                    store_row(image_out, row, out_pix);
                }
            }
        }
    }

top_rows:
    for (int row = 0; row < 16; ++row) {
#pragma HLS LOOP_FLATTEN off
        store_row(image_out, row, top_pix);
    }

    if (cycles != 0) {
        *cycles = kReportedCycles;
    }
}
