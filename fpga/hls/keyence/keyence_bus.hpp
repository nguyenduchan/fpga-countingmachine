// DDR burst shell shared by the Keyence HLS tops.
// Image words are 16 pixels. The ready counter matches tile_brightness:
// byte offset 0x100000, incremented by the CPU after each raw scanline.

#ifndef KEYENCE_BUS_HPP
#define KEYENCE_BUS_HPP

#include <stdint.h>

#include "ap_int.h"
#include "keyence_scan.hpp"

static const int kKeyBeatPixels = 16;
static const int kKeyChunkWords = 16;
static const int kKeyWordsPerRow = kKeyWidth / kKeyBeatPixels;
static const int kKeyChunksPerRow = kKeyWordsPerRow / kKeyChunkWords;
static const int kKeyReadyWord = 0x100000 / 16;
// One 128-bit beat is 16 pixels. Load, filter and store are one beat per cycle.
static const int kKeyCyclesPerRow = 240;
static const uint32_t kKeyPassCycles = 240u * 800u;

static void keyence_wait_rows(const ap_uint<128>* image_in, int rows_ready) {
    while (true) {
#pragma HLS PIPELINE off
        const ap_uint<128> ready_word = image_in[kKeyReadyWord];
        if (static_cast<int>(ready_word.range(31, 0)) >= rows_ready) {
            break;
        }
    }
}

static void keyence_read_chunk(const ap_uint<128>* src, int word_base, ap_uint<128> beats[kKeyChunkWords]) {
#pragma HLS INLINE off
    for (int word = 0; word < kKeyChunkWords; ++word) {
#pragma HLS PIPELINE II=1
        beats[word] = src[word_base + word];
    }
}

static void keyence_write_chunk(ap_uint<128>* dst, int word_base, const ap_uint<128> beats[kKeyChunkWords]) {
#pragma HLS INLINE off
    for (int word = 0; word < kKeyChunkWords; ++word) {
#pragma HLS PIPELINE II=1
        dst[word_base + word] = beats[word];
    }
}

static void keyence_load_row(const ap_uint<128>* src, int y, uint8_t row[kKeyWidth]) {
#pragma HLS INLINE
#pragma HLS ARRAY_PARTITION variable=row cyclic factor=16
    for (int word = 0; word < kKeyWordsPerRow; ++word) {
#pragma HLS PIPELINE II=1
        const ap_uint<128> beat = src[y * kKeyWordsPerRow + word];
        for (int lane = 0; lane < kKeyBeatPixels; ++lane) {
#pragma HLS UNROLL
            row[word * kKeyBeatPixels + lane] = beat.range(lane * 8 + 7, lane * 8);
        }
    }
}

static void keyence_store_row(ap_uint<128>* dst, int y, const uint8_t row[kKeyWidth]) {
#pragma HLS INLINE
#pragma HLS ARRAY_PARTITION variable=row cyclic factor=16
    for (int word = 0; word < kKeyWordsPerRow; ++word) {
#pragma HLS PIPELINE II=1
        ap_uint<128> beat = 0;
        for (int lane = 0; lane < kKeyBeatPixels; ++lane) {
#pragma HLS UNROLL
            const int x = word * kKeyBeatPixels + lane;
            beat.range(lane * 8 + 7, lane * 8) = row[x];
        }
        dst[y * kKeyWordsPerRow + word] = beat;
    }
}

static int keyence_frame_ok(int width, int height, uint32_t* cycles) {
    if (width == kKeyWidth && height == kKeyHeight) {
        return 1;
    }
    if (cycles != 0) {
        *cycles = 0;
    }
    return 0;
}

template <typename Tag>
void keyence_hls_window(const ap_uint<128>* image_in, ap_uint<128>* image_out,
                        int width, int height, int p0, int p1, int wait_rows, uint32_t* cycles) {
    if (!keyence_frame_ok(width, height, cycles)) {
        return;
    }
    uint8_t ring[3][kKeyWidth];
    uint8_t dst[kKeyWidth];
#pragma HLS ARRAY_PARTITION variable=ring complete dim=1
#pragma HLS ARRAY_PARTITION variable=ring cyclic factor=16 dim=2
#pragma HLS ARRAY_PARTITION variable=dst cyclic factor=16

    if (wait_rows) {
        keyence_wait_rows(image_in, 1);
    }
    keyence_load_row(image_in, 0, ring[0]);
    keyence_load_row(image_in, 0, ring[1]);

    for (int y = 0; y < kKeyHeight; ++y) {
#pragma HLS LOOP_FLATTEN off
        const int slot0 = y % 3;
        const int slot1 = (y + 1) % 3;
        const int slot2 = (y + 2) % 3;
        if (y + 1 < kKeyHeight) {
            if (wait_rows) {
                keyence_wait_rows(image_in, y + 2);
            }
            keyence_load_row(image_in, y + 1, ring[slot2]);
        } else {
            keyence_load_row(image_in, kKeyHeight - 1, ring[slot2]);
        }
        keyence_emit3<Tag>(ring[slot0], ring[slot1], ring[slot2], dst, kKeyWidth, p0, p1);
        keyence_store_row(image_out, y, dst);
    }
    if (cycles != 0) {
        *cycles = kKeyPassCycles;
    }
}

template <typename Tag>
void keyence_hls_point(const ap_uint<128>* image_in, ap_uint<128>* image_out,
                       int width, int height, int p0, int p1, int wait_rows, uint32_t* cycles) {
    if (!keyence_frame_ok(width, height, cycles)) {
        return;
    }
    uint8_t src[kKeyWidth];
    uint8_t dst[kKeyWidth];
#pragma HLS ARRAY_PARTITION variable=src cyclic factor=16
#pragma HLS ARRAY_PARTITION variable=dst cyclic factor=16
    for (int y = 0; y < kKeyHeight; ++y) {
#pragma HLS LOOP_FLATTEN off
        if (wait_rows) {
            keyence_wait_rows(image_in, y + 1);
        }
        keyence_load_row(image_in, y, src);
        keyence_emit1<Tag>(src, dst, kKeyWidth, p0, p1);
        keyence_store_row(image_out, y, dst);
    }
    if (cycles != 0) {
        *cycles = kKeyPassCycles;
    }
}

static const int kKeyLabelBeats = kKeyWidth / 8;

static void keyence_load_labels(const ap_uint<128>* src, int y, uint16_t row[kKeyWidth]) {
    for (int word = 0; word < kKeyLabelBeats; ++word) {
#pragma HLS PIPELINE II=1
        const ap_uint<128> beat = src[y * kKeyLabelBeats + word];
        for (int lane = 0; lane < 8; ++lane) {
#pragma HLS UNROLL
            row[word * 8 + lane] = static_cast<uint16_t>(beat.range(lane * 16 + 15, lane * 16));
        }
    }
}

static void keyence_store_labels(ap_uint<128>* dst, int y, const uint16_t row[kKeyWidth]) {
    for (int word = 0; word < kKeyLabelBeats; ++word) {
#pragma HLS PIPELINE II=1
        ap_uint<128> beat = 0;
        for (int lane = 0; lane < 8; ++lane) {
#pragma HLS UNROLL
            beat.range(lane * 16 + 15, lane * 16) = row[word * 8 + lane];
        }
        dst[y * kKeyLabelBeats + word] = beat;
    }
}

template <typename Tag>
void keyence_hls_pair(const ap_uint<128>* current, const ap_uint<128>* other, ap_uint<128>* image_out,
                      int width, int height, int p0, int p1, int wait_rows, uint32_t* cycles) {
    if (!keyence_frame_ok(width, height, cycles)) {
        return;
    }
    uint8_t row_a[kKeyWidth];
    uint8_t row_b[kKeyWidth];
    uint8_t dst[kKeyWidth];
#pragma HLS ARRAY_PARTITION variable=row_a cyclic factor=16
#pragma HLS ARRAY_PARTITION variable=row_b cyclic factor=16
#pragma HLS ARRAY_PARTITION variable=dst cyclic factor=16
    for (int y = 0; y < kKeyHeight; ++y) {
#pragma HLS LOOP_FLATTEN off
        if (wait_rows) {
            keyence_wait_rows(current, y + 1);
        }
        keyence_load_row(current, y, row_a);
        keyence_load_row(other, y, row_b);
        keyence_emit2<Tag>(row_a, row_b, dst, kKeyWidth, p0, p1);
        keyence_store_row(image_out, y, dst);
    }
    if (cycles != 0) {
        *cycles = kKeyPassCycles;
    }
}

#endif
