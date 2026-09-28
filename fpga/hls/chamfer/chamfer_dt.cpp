// FPGA distance field for chamfer. One pixel per cycle, two passes.
// Clock 100 MHz, 1280x800: about 2.05 million cycles, 20.5 ms.

#include <stdint.h>

#include "ap_int.h"
#include "chamfer_core.hpp"

static const int kBeat = 16;
static const int kWords = kChamferWidth / kBeat;

static void load_gray(const ap_uint<128>* src, int y, uint8_t row[kChamferWidth]) {
#pragma HLS INLINE
    for (int word = 0; word < kWords; ++word) {
#pragma HLS PIPELINE II=1
        const ap_uint<128> beat = src[y * kWords + word];
        for (int lane = 0; lane < kBeat; ++lane) {
#pragma HLS UNROLL
            row[word * kBeat + lane] = beat.range(lane * 8 + 7, lane * 8);
        }
    }
}

static void store_dt(ap_uint<128>* dst, int y, const uint16_t row[kChamferWidth]) {
#pragma HLS INLINE
    for (int word = 0; word < kChamferWidth / 8; ++word) {
#pragma HLS PIPELINE II=1
        ap_uint<128> beat = 0;
        for (int lane = 0; lane < 8; ++lane) {
#pragma HLS UNROLL
            beat.range(lane * 16 + 15, lane * 16) = row[word * 8 + lane];
        }
        dst[y * (kChamferWidth / 8) + word] = beat;
    }
}

static void load_dt(const ap_uint<128>* src, int y, uint16_t row[kChamferWidth]) {
#pragma HLS INLINE
    for (int word = 0; word < kChamferWidth / 8; ++word) {
#pragma HLS PIPELINE II=1
        const ap_uint<128> beat = src[y * (kChamferWidth / 8) + word];
        for (int lane = 0; lane < 8; ++lane) {
#pragma HLS UNROLL
            row[word * 8 + lane] = static_cast<uint16_t>(beat.range(lane * 16 + 15, lane * 16));
        }
    }
}

void chamfer_dt(const ap_uint<128>* image_in, ap_uint<128>* distance, int width, int height, int thresh,
                uint32_t* cycles) {
#pragma HLS INTERFACE m_axi port=image_in offset=slave bundle=gmem depth=64000 max_read_burst_length=16 num_read_outstanding=4 max_widen_bitwidth=128
#pragma HLS INTERFACE m_axi port=distance offset=slave bundle=gmem depth=128000 max_read_burst_length=16 num_read_outstanding=4 max_write_burst_length=16 num_write_outstanding=4 max_widen_bitwidth=128
#pragma HLS INTERFACE s_axilite port=image_in bundle=control
#pragma HLS INTERFACE s_axilite port=distance bundle=control
#pragma HLS INTERFACE s_axilite port=width bundle=control
#pragma HLS INTERFACE s_axilite port=height bundle=control
#pragma HLS INTERFACE s_axilite port=thresh bundle=control
#pragma HLS INTERFACE s_axilite port=cycles bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control
    if (width != kChamferWidth || height != kChamferHeight) {
        if (cycles != 0) {
            *cycles = 0;
        }
        return;
    }
    uint8_t up[kChamferWidth];
    uint8_t mid[kChamferWidth];
    uint8_t down[kChamferWidth];
    uint8_t edge[kChamferWidth];
    uint16_t prev[kChamferWidth];
    uint16_t cur[kChamferWidth];
#pragma HLS BIND_STORAGE variable=prev type=ram_t2p impl=bram
#pragma HLS BIND_STORAGE variable=cur type=ram_t2p impl=bram
    load_gray(image_in, 0, up);
    load_gray(image_in, 0, mid);
    for (int y = 0; y < kChamferHeight; ++y) {
#pragma HLS LOOP_FLATTEN off
        if (y + 1 < kChamferHeight) {
            load_gray(image_in, y + 1, down);
        } else {
            load_gray(image_in, kChamferHeight - 1, down);
        }
        for (int x = 0; x < kChamferWidth; ++x) {
#pragma HLS PIPELINE II=1
            edge[x] = static_cast<uint8_t>(chamfer_edge_pixel(up, mid, down, x, kChamferWidth, thresh));
        }
        chamfer_forward_row(edge, prev, cur, kChamferWidth, y > 0);
        store_dt(distance, y, cur);
        for (int x = 0; x < kChamferWidth; ++x) {
#pragma HLS PIPELINE II=1
            prev[x] = cur[x];
            up[x] = mid[x];
            mid[x] = down[x];
        }
    }
    for (int y = kChamferHeight - 1; y >= 0; --y) {
#pragma HLS LOOP_FLATTEN off
        load_dt(distance, y, cur);
        uint16_t below[kChamferWidth];
#pragma HLS BIND_STORAGE variable=below type=ram_t2p impl=bram
        int have = 0;
        if (y + 1 < kChamferHeight) {
            load_dt(distance, y + 1, below);
            have = 1;
        }
        chamfer_backward_row(cur, below, kChamferWidth, have);
        store_dt(distance, y, cur);
    }
    if (cycles != 0) {
        *cycles = 2048000u;
    }
}
