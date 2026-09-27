// FPGA kernel for the Kria KV260. One scan of the 1280x800 frame.
// Each 32-row band is read once into a small on-chip buffer while Sum and
// SumSq of its forty 32x32 tiles are accumulated. Gain and offset for that
// band are computed immediately, then the band is written back. The frame is
// not read from DDR a second time.
//
// Sixteen pixels move on each 128-bit beat. Bursts stay 16 words so they do
// not cross a 4 KB boundary. There is no while loop.

#include <stdint.h>

#include "ap_int.h"

static const int kWidth = 1280;
static const int kHeight = 800;
static const int kTile = 32;
static const int kBeatPixels = 16;
static const int kTilesX = kWidth / kTile;
static const int kTilesY = kHeight / kTile;
static const int kWordsPerRow = kWidth / kBeatPixels;
static const int kChunkWords = 16;
static const int kChunksPerRow = kWordsPerRow / kChunkWords;
static const int kBandPixels = kTile * kWidth;

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

static ap_uint<128> level_beat(ap_uint<128> beat, uint16_t gain_q8, int16_t offset) {
#pragma HLS INLINE
    ap_uint<128> leveled = 0;
engines:
    for (int lane = 0; lane < kBeatPixels; ++lane) {
#pragma HLS UNROLL
        const uint32_t pixel = beat.range(lane * 8 + 7, lane * 8);
        const int32_t scaled = (static_cast<int32_t>(pixel) * static_cast<int32_t>(gain_q8)) >> 8;
        int32_t value = scaled + static_cast<int32_t>(offset);
        if (value < 0) {
            value = 0;
        } else if (value > 255) {
            value = 255;
        }
        leveled.range(lane * 8 + 7, lane * 8) = static_cast<ap_uint<8>>(value);
    }
    return leveled;
}

static void read_chunk(const ap_uint<128>* src, int word_base, int tile_x0, ap_uint<128> row[kWordsPerRow], int row_word,
                      uint32_t sums[kTilesX], uint32_t sumsq[kTilesX]) {
#pragma HLS INLINE off
    uint32_t pair_sum = 0;
    uint32_t pair_sq = 0;
read_words:
    for (int word = 0; word < kChunkWords; ++word) {
#pragma HLS PIPELINE II=1
        const ap_uint<128> beat = src[word_base + word];
        row[row_word + word] = beat;
        uint32_t beat_sum = 0;
        uint32_t beat_sq = 0;
        beat_moments(beat, beat_sum, beat_sq);
        if ((word & 1) == 0) {
            pair_sum = beat_sum;
            pair_sq = beat_sq;
        } else {
            const int tile_x = tile_x0 + (word >> 1);
            sums[tile_x] += pair_sum + beat_sum;
            sumsq[tile_x] += pair_sq + beat_sq;
        }
    }
}

static void write_chunk(ap_uint<128>* dst, int word_base, int tile_x0, const ap_uint<128> row[kWordsPerRow], int row_word,
                       const uint16_t gains[kTilesX], const int16_t offsets[kTilesX]) {
#pragma HLS INLINE off
    ap_uint<128> out[kChunkWords];
level_words:
    for (int word = 0; word < kChunkWords; ++word) {
#pragma HLS PIPELINE II=1
        const int tile_x = tile_x0 + (word >> 1);
        out[word] = level_beat(row[row_word + word], gains[tile_x], offsets[tile_x]);
    }
store_words:
    for (int word = 0; word < kChunkWords; ++word) {
#pragma HLS PIPELINE II=1
        dst[word_base + word] = out[word];
    }
}

static void read_band(const ap_uint<128>* src, int band, ap_uint<128> rows[kTile][kWordsPerRow], uint32_t sums[kTilesX],
                      uint32_t sumsq[kTilesX]) {
#pragma HLS INLINE off
read_rows:
    for (int row = 0; row < kTile; ++row) {
#pragma HLS LOOP_FLATTEN off
    read_chunks:
        for (int chunk = 0; chunk < kChunksPerRow; ++chunk) {
#pragma HLS LOOP_FLATTEN off
            const int word_base = (band * kTile + row) * kWordsPerRow + chunk * kChunkWords;
            read_chunk(src, word_base, chunk * (kChunkWords / 2), rows[row], chunk * kChunkWords, sums, sumsq);
        }
    }
}

static void write_band(ap_uint<128>* dst, int band, ap_uint<128> rows[kTile][kWordsPerRow], const uint16_t gains[kTilesX],
                       const int16_t offsets[kTilesX]) {
#pragma HLS INLINE off
write_rows:
    for (int row = 0; row < kTile; ++row) {
#pragma HLS LOOP_FLATTEN off
    write_chunks:
        for (int chunk = 0; chunk < kChunksPerRow; ++chunk) {
#pragma HLS LOOP_FLATTEN off
            const int word_base = (band * kTile + row) * kWordsPerRow + chunk * kChunkWords;
            write_chunk(dst, word_base, chunk * (kChunkWords / 2), rows[row], chunk * kChunkWords, gains, offsets);
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

    if (width != kWidth || height != kHeight || tile_w != kTile || tile_h != kTile) {
        if (cycles != 0) {
            *cycles = 0;
        }
        return;
    }

    ap_uint<128> band_rows[kTile][kWordsPerRow];
#pragma HLS BIND_STORAGE variable=band_rows type=ram_t2p impl=uram

    uint64_t seen = 0;
    uint64_t seen_sq = 0;

bands:
    for (int band = 0; band < kTilesY; ++band) {
#pragma HLS LOOP_FLATTEN off
        uint32_t sums[kTilesX];
        uint32_t sumsq[kTilesX];
        uint16_t gains[kTilesX];
        int16_t offsets[kTilesX];
    clear_band:
        for (int tile_x = 0; tile_x < kTilesX; ++tile_x) {
#pragma HLS PIPELINE II=1
            sums[tile_x] = 0;
            sumsq[tile_x] = 0;
        }

        read_band(image_in, band, band_rows, sums, sumsq);

        uint64_t band_sum = 0;
        uint64_t band_sq = 0;
    fold_band:
        for (int tile_x = 0; tile_x < kTilesX; ++tile_x) {
#pragma HLS PIPELINE II=1
            band_sum += sums[tile_x];
            band_sq += sumsq[tile_x];
        }
        seen += band_sum;
        seen_sq += band_sq;

        const uint32_t pixels_seen = static_cast<uint32_t>(band + 1) * static_cast<uint32_t>(kBandPixels);
        const uint32_t mu_target = static_cast<uint32_t>(seen / pixels_seen);
        const uint64_t energy = seen_sq / pixels_seen;
        const uint64_t mean_sq = static_cast<uint64_t>(mu_target) * mu_target;
        const uint32_t var_target = (energy > mean_sq) ? static_cast<uint32_t>(energy - mean_sq) : 0u;
        const uint32_t sigma_target = isqrt(var_target);
        const uint32_t sigma_target_safe = (sigma_target == 0) ? 1u : sigma_target;

    gain_tiles:
        for (int tile_x = 0; tile_x < kTilesX; ++tile_x) {
#pragma HLS PIPELINE II=1
            const uint32_t mean = sums[tile_x] >> 10;
            const uint32_t tile_energy = sumsq[tile_x] >> 10;
            const uint32_t tile_mean_sq = mean * mean;
            const uint32_t variance = (tile_energy > tile_mean_sq) ? (tile_energy - tile_mean_sq) : 0u;
            const uint32_t sigma = isqrt(variance);
            const uint32_t sigma_safe = (sigma == 0) ? sigma_target_safe : sigma;
            const uint32_t gain_q8 = (sigma_target_safe << 8) / sigma_safe;
            const int32_t offset = static_cast<int32_t>(mu_target) - static_cast<int32_t>((mean * gain_q8) >> 8);
            gains[tile_x] = static_cast<uint16_t>(gain_q8 > 65535u ? 65535u : gain_q8);
            if (offset < -32768) {
                offsets[tile_x] = -32768;
            } else if (offset > 32767) {
                offsets[tile_x] = 32767;
            } else {
                offsets[tile_x] = static_cast<int16_t>(offset);
            }
        }

        write_band(image_out, band, band_rows, gains, offsets);
    }

    if (cycles != 0) {
        *cycles = static_cast<uint32_t>(kHeight * kWordsPerRow * 2);
    }
}
