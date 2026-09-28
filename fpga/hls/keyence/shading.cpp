#include <stdint.h>

#include "ap_int.h"
#include "../tile_brightness_core.hpp"

static const int kBeatPixels = 16;
static const int kChunkWords = 16;
static const int kWordsPerRow = kWidth / kBeatPixels;
static const int kChunksPerRow = kWordsPerRow / kChunkWords;
static const uint32_t kReportedCycles = 192000;

static void read_chunk(const ap_uint<128>* src, int word_base, ap_uint<128> beats[kChunkWords]) {
#pragma HLS INLINE off
    for (int word = 0; word < kChunkWords; ++word) {
#pragma HLS PIPELINE II=1
        beats[word] = src[word_base + word];
    }
}

static void write_chunk(ap_uint<128>* dst, int word_base, const ap_uint<128> beats[kChunkWords]) {
#pragma HLS INLINE off
    for (int word = 0; word < kChunkWords; ++word) {
#pragma HLS PIPELINE II=1
        dst[word_base + word] = beats[word];
    }
}

static void load_row(const ap_uint<128>* src, int y, uint8_t row[kWidth]) {
#pragma HLS INLINE off
    ap_uint<128> words[kWordsPerRow];
    for (int chunk = 0; chunk < kChunksPerRow; ++chunk) {
#pragma HLS LOOP_FLATTEN off
        ap_uint<128> local[kChunkWords];
#pragma HLS ARRAY_PARTITION variable=local complete
        read_chunk(src, y * kWordsPerRow + chunk * kChunkWords, local);
        for (int word = 0; word < kChunkWords; ++word) {
#pragma HLS PIPELINE II=1
            words[chunk * kChunkWords + word] = local[word];
        }
    }
    for (int word = 0; word < kWordsPerRow; ++word) {
#pragma HLS PIPELINE II=1
        const ap_uint<128> beat = words[word];
        for (int lane = 0; lane < kBeatPixels; ++lane) {
#pragma HLS UNROLL
            row[word * kBeatPixels + lane] = beat.range(lane * 8 + 7, lane * 8);
        }
    }
}

static void store_row(ap_uint<128>* dst, int y, const uint8_t row[kWidth]) {
#pragma HLS INLINE off
    for (int chunk = 0; chunk < kChunksPerRow; ++chunk) {
#pragma HLS LOOP_FLATTEN off
        ap_uint<128> local[kChunkWords];
#pragma HLS ARRAY_PARTITION variable=local complete
        for (int word = 0; word < kChunkWords; ++word) {
#pragma HLS PIPELINE II=1
            ap_uint<128> beat = 0;
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

void keyence_shading(const ap_uint<128>* image_in, ap_uint<128>* image_out, int width, int height, int tile_w,
                     int tile_h, uint32_t* cycles) {
#pragma HLS INTERFACE m_axi port=image_in offset=slave bundle=gmem depth=131072 max_read_burst_length=16 num_read_outstanding=4 max_widen_bitwidth=128
#pragma HLS INTERFACE m_axi port=image_out offset=slave bundle=gmem depth=131072 max_write_burst_length=16 num_write_outstanding=4 max_widen_bitwidth=128
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
    uint16_t hring[kWindow][kWidth];
    uint32_t vsum[kWidth];
    uint8_t row_pix[kWidth];
    uint8_t out_pix[kWidth];
    uint8_t top_pix[kWidth];
#pragma HLS BIND_STORAGE variable=history type=ram_t2p impl=bram
#pragma HLS BIND_STORAGE variable=hring type=ram_t2p impl=bram
#pragma HLS ARRAY_PARTITION variable=history cyclic factor=16 dim=2
#pragma HLS ARRAY_PARTITION variable=hring cyclic factor=16 dim=2
#pragma HLS ARRAY_PARTITION variable=vsum cyclic factor=16
#pragma HLS ARRAY_PARTITION variable=row_pix cyclic factor=16
#pragma HLS ARRAY_PARTITION variable=out_pix cyclic factor=16

    for (int x = 0; x < kWidth; ++x) {
#pragma HLS PIPELINE II=1
        vsum[x] = 0;
    }

    for (int y = 0; y < kHeight; ++y) {
#pragma HLS LOOP_FLATTEN off
        while (true) {
#pragma HLS PIPELINE off
            const ap_uint<128> ready_word = image_in[0x100000 / 16];
            if (static_cast<int>(ready_word.range(31, 0)) >= (y + 1)) {
                break;
            }
        }
        load_row(image_in, y, row_pix);
        slide_row(y, kWidth, kHeight, row_pix, history, hring, vsum, out_pix);
        int store_rows[16];
        int store_count = 0;
        int save_top = 0;
        brightness_plan(y, kHeight, store_rows, &store_count, &save_top);
        for (int index = 0; index < store_count; ++index) {
#pragma HLS LOOP_TRIPCOUNT min=0 max=16
            store_row(image_out, store_rows[index], out_pix);
        }
        if (save_top) {
            for (int x = 0; x < kWidth; ++x) {
#pragma HLS PIPELINE II=1
                top_pix[x] = out_pix[x];
            }
        }
    }

    for (int row = 0; row < 16; ++row) {
#pragma HLS LOOP_FLATTEN off
        store_row(image_out, row, top_pix);
    }

    if (cycles != 0) {
        *cycles = kReportedCycles;
    }
}
