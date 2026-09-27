// FPGA kernel for the Kria KV260.
// Brightness is leveled per 32x32 tile, then the gain and offset applied to
// each pixel are bilinearly interpolated from the four surrounding tile
// centers. A pixel on an outer edge blends only the two nearest centers.
// A corner pixel uses that corner tile directly. That removes the visible
// square boundaries of a constant gain inside each tile.
//
// The frame is read twice from DDR: once to collect Sum and SumSq, then
// again to scale and write. Bursts stay 16 words so they do not cross 4 KB.
// There is no while loop.

#include <stdint.h>

#include "ap_int.h"

static const int kWidth = 1280;
static const int kHeight = 800;
static const int kTile = 32;
static const int kBeatPixels = 16;
static const int kTilesX = kWidth / kTile;
static const int kTilesY = kHeight / kTile;
static const int kTileCount = kTilesX * kTilesY;
static const int kWordsPerRow = kWidth / kBeatPixels;
static const int kChunkWords = 16;
static const int kChunksPerRow = kWordsPerRow / kChunkWords;

static uint32_t square8(uint32_t pixel) {
#pragma HLS INLINE
    const uint32_t product = pixel * pixel;
#pragma HLS BIND_OP variable=product op=mul impl=fabric
    return product;
}

static void beat_moments(ap_uint<128> word, uint32_t& sum, uint32_t& sum_sq) {
#pragma HLS INLINE
    uint32_t partial_sum = 0;
    uint32_t partial_sq = 0;
lanes:
    for (int lane = 0; lane < kBeatPixels; ++lane) {
#pragma HLS UNROLL
        const uint32_t pixel = word.range(lane * 8 + 7, lane * 8);
        partial_sum += pixel;
        partial_sq += square8(pixel);
    }
    sum = partial_sum;
    sum_sq = partial_sq;
}

static uint16_t isqrt(uint32_t value) {
#pragma HLS INLINE
    uint16_t result = 0;
    uint16_t bit = 128;
sqrt_steps:
    for (int step = 0; step < 8; ++step) {
#pragma HLS UNROLL
        const uint16_t trial = static_cast<uint16_t>(result + bit);
        const uint32_t squared = static_cast<uint32_t>(trial) * trial;
#pragma HLS BIND_OP variable=squared op=mul impl=fabric
        if (squared <= value) {
            result = trial;
        }
        bit = static_cast<uint16_t>(bit >> 1);
    }
    return result;
}

static int32_t blend_q5(int32_t left, int32_t right, int delta) {
#pragma HLS INLINE
    return ((32 - delta) * left + delta * right) >> 5;
}

// Tile centers sit 16 pixels in from each 32-pixel edge.
// delta is the distance past the left/top center, in 0..31.
static void center_axis(int coord, int tile_count, int& tile, int& delta, bool& pair) {
#pragma HLS INLINE
    if (coord < 16) {
        tile = 0;
        delta = 0;
        pair = false;
        return;
    }
    const int relative = coord - 16;
    int index = relative >> 5;
    const int fraction = relative & 31;
    if (index >= tile_count - 1) {
        tile = tile_count - 1;
        delta = 0;
        pair = false;
        return;
    }
    tile = index;
    delta = fraction;
    pair = true;
}

static void level_beat(ap_uint<128> beat, int x_base, int tx, int dx0, bool x_pair, int32_t gain_left,
                       int32_t gain_right, int32_t offset_left, int32_t offset_right, ap_uint<128>& leveled) {
#pragma HLS INLINE
    (void)x_base;
    leveled = 0;
lanes:
    for (int lane = 0; lane < kBeatPixels; ++lane) {
#pragma HLS UNROLL
        const int delta = x_pair ? (dx0 + lane) : 0;
        const int32_t gain = blend_q5(gain_left, gain_right, delta);
        const int32_t offset = blend_q5(offset_left, offset_right, delta);
        const uint32_t pixel = beat.range(lane * 8 + 7, lane * 8);
        const int32_t scaled = (static_cast<int32_t>(pixel) * gain) >> 8;
        int32_t value = scaled + offset;
        if (value < 0) {
            value = 0;
        } else if (value > 255) {
            value = 255;
        }
        leveled.range(lane * 8 + 7, lane * 8) = static_cast<ap_uint<8>>(value);
    }
}

static void accumulate_chunk(const ap_uint<128>* src, int word_base, int tile_y, int tile_x0,
                             uint32_t sums[kTileCount], uint32_t sumsq[kTileCount]) {
#pragma HLS INLINE off
    uint32_t pair_sum = 0;
    uint32_t pair_sq = 0;
absorb:
    for (int word = 0; word < kChunkWords; ++word) {
#pragma HLS PIPELINE II=1
        const ap_uint<128> beat = src[word_base + word];
        uint32_t beat_sum = 0;
        uint32_t beat_sq = 0;
        beat_moments(beat, beat_sum, beat_sq);
        if ((word & 1) == 0) {
            pair_sum = beat_sum;
            pair_sq = beat_sq;
        } else {
            const int tile = tile_y * kTilesX + tile_x0 + (word >> 1);
            sums[tile] += pair_sum + beat_sum;
            sumsq[tile] += pair_sq + beat_sq;
        }
    }
}

static void scale_chunk(const ap_uint<128>* src, ap_uint<128>* dst, int word_base, int x_base,
                        const int32_t row_gain[kTilesX], const int32_t row_offset[kTilesX]) {
#pragma HLS INLINE off
#pragma HLS ARRAY_PARTITION variable=row_gain complete dim=1
#pragma HLS ARRAY_PARTITION variable=row_offset complete dim=1
    ap_uint<128> out[kChunkWords];
level_words:
    for (int word = 0; word < kChunkWords; ++word) {
#pragma HLS PIPELINE II=1
        const int beat_x = x_base + word * kBeatPixels;
        int tile_x = 0;
        int dx0 = 0;
        bool x_pair = false;
        center_axis(beat_x, kTilesX, tile_x, dx0, x_pair);
        const int right = x_pair ? (tile_x + 1) : tile_x;
        ap_uint<128> leveled = 0;
        level_beat(src[word_base + word], beat_x, tile_x, dx0, x_pair, row_gain[tile_x], row_gain[right],
                   row_offset[tile_x], row_offset[right], leveled);
        out[word] = leveled;
    }
store_words:
    for (int word = 0; word < kChunkWords; ++word) {
#pragma HLS PIPELINE II=1
        dst[word_base + word] = out[word];
    }
}

static void scale_row(const ap_uint<128>* src, ap_uint<128>* dst, int y, const uint16_t gains[kTileCount],
                      const int16_t offsets[kTileCount]) {
#pragma HLS INLINE off
    int tile_y = 0;
    int dy = 0;
    bool y_pair = false;
    center_axis(y, kTilesY, tile_y, dy, y_pair);

    int32_t row_gain[kTilesX];
    int32_t row_offset[kTilesX];
#pragma HLS ARRAY_PARTITION variable=row_gain complete dim=1
#pragma HLS ARRAY_PARTITION variable=row_offset complete dim=1
blend_columns:
    for (int tile_x = 0; tile_x < kTilesX; ++tile_x) {
#pragma HLS UNROLL
        const int top = tile_y * kTilesX + tile_x;
        const int bottom = y_pair ? (top + kTilesX) : top;
        row_gain[tile_x] = blend_q5(gains[top], gains[bottom], y_pair ? dy : 0);
        row_offset[tile_x] = blend_q5(offsets[top], offsets[bottom], y_pair ? dy : 0);
    }

scale_chunks:
    for (int chunk = 0; chunk < kChunksPerRow; ++chunk) {
#pragma HLS LOOP_FLATTEN off
        const int word_base = y * kWordsPerRow + chunk * kChunkWords;
        const int x_base = chunk * kChunkWords * kBeatPixels;
        scale_chunk(src, dst, word_base, x_base, row_gain, row_offset);
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

    if (width != kWidth || height != kHeight || tile_w != kTile || tile_h != kTile) {
        if (cycles != 0) {
            *cycles = 0;
        }
        return;
    }

    uint32_t sums[kTileCount];
    uint32_t sumsq[kTileCount];
    uint16_t gains[kTileCount];
    int16_t offsets[kTileCount];

clear_tiles:
    for (int tile = 0; tile < kTileCount; ++tile) {
#pragma HLS PIPELINE II=1
        sums[tile] = 0;
        sumsq[tile] = 0;
    }

load_rows:
    for (int y = 0; y < kHeight; ++y) {
#pragma HLS LOOP_FLATTEN off
    load_chunks:
        for (int chunk = 0; chunk < kChunksPerRow; ++chunk) {
#pragma HLS LOOP_FLATTEN off
            const int word_base = y * kWordsPerRow + chunk * kChunkWords;
            accumulate_chunk(image_in, word_base, y >> 5, chunk * (kChunkWords / 2), sums, sumsq);
        }
    }

    uint64_t total = 0;
    uint64_t total_sq = 0;
sum_blocks:
    for (int tile = 0; tile < kTileCount; ++tile) {
#pragma HLS PIPELINE II=1
        total += sums[tile];
        total_sq += sumsq[tile];
    }

    const uint32_t mu_target = static_cast<uint32_t>(total >> 20);
    const uint64_t energy_target = total_sq >> 20;
    const uint64_t mean_sq_target = static_cast<uint64_t>(mu_target) * mu_target;
    const uint32_t var_target =
        (energy_target > mean_sq_target) ? static_cast<uint32_t>(energy_target - mean_sq_target) : 0u;
    const uint32_t sigma_target = isqrt(var_target);
    const uint32_t sigma_target_safe = (sigma_target == 0) ? 1u : sigma_target;

gain_blocks:
    for (int tile = 0; tile < kTileCount; ++tile) {
#pragma HLS PIPELINE II=1
        const uint32_t mean = sums[tile] >> 10;
        const uint32_t energy = sumsq[tile] >> 10;
        const uint32_t mean_sq = mean * mean;
        const uint32_t variance = (energy > mean_sq) ? (energy - mean_sq) : 0u;
        const uint32_t sigma = isqrt(variance);
        const uint32_t sigma_safe = (sigma == 0) ? sigma_target_safe : sigma;
        const uint32_t gain_q8 = (sigma_target_safe << 8) / sigma_safe;
        const int32_t offset = static_cast<int32_t>(mu_target) - static_cast<int32_t>((mean * gain_q8) >> 8);
        gains[tile] = static_cast<uint16_t>(gain_q8 > 65535u ? 65535u : gain_q8);
        if (offset < -32768) {
            offsets[tile] = -32768;
        } else if (offset > 32767) {
            offsets[tile] = 32767;
        } else {
            offsets[tile] = static_cast<int16_t>(offset);
        }
    }

scale_rows:
    for (int y = 0; y < kHeight; ++y) {
#pragma HLS LOOP_FLATTEN off
        scale_row(image_in, image_out, y, gains, offsets);
    }

    if (cycles != 0) {
        *cycles = static_cast<uint32_t>(kHeight * kWordsPerRow * 2);
    }
}
