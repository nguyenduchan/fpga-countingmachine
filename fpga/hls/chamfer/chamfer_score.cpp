// Coarse chamfer score grid. Each search site samples the template edge list.
// At stride 8 and 64 points this is about 1 million samples, 10 ms at one sample per cycle.
// Peak refine stays on the CPU: a few dozen windows, not the whole frame.

#include <stdint.h>

#include "ap_int.h"
#include "chamfer_core.hpp"

static void load_points(const ap_uint<128>* src, int n, int16_t dst[kChamferMaxPoints]) {
    const int words = (n + 7) / 8;
    int filled = 0;
    for (int word = 0; word < words; ++word) {
#pragma HLS PIPELINE II=1
        const ap_uint<128> beat = src[word];
        for (int lane = 0; lane < 8; ++lane) {
#pragma HLS UNROLL
            if (filled < n && filled < kChamferMaxPoints) {
                dst[filled] = static_cast<int16_t>(beat.range(lane * 16 + 15, lane * 16));
                filled += 1;
            }
        }
    }
}

void chamfer_score(const ap_uint<128>* distance, const ap_uint<128>* rdx_bus, const ap_uint<128>* rdy_bus,
                   ap_uint<128>* scores, int width, int height, int n_pts, int half_w, int half_h, int stride,
                   uint32_t* cycles) {
#pragma HLS INTERFACE m_axi port=distance offset=slave bundle=gmem depth=128000 max_read_burst_length=16 num_read_outstanding=16 max_widen_bitwidth=128
#pragma HLS INTERFACE m_axi port=rdx_bus offset=slave bundle=gmem depth=16 max_read_burst_length=16 num_read_outstanding=4 max_widen_bitwidth=128
#pragma HLS INTERFACE m_axi port=rdy_bus offset=slave bundle=gmem depth=16 max_read_burst_length=16 num_read_outstanding=4 max_widen_bitwidth=128
#pragma HLS INTERFACE m_axi port=scores offset=slave bundle=gmem depth=2048 max_write_burst_length=16 num_write_outstanding=4 max_widen_bitwidth=128
#pragma HLS INTERFACE s_axilite port=distance bundle=control
#pragma HLS INTERFACE s_axilite port=rdx_bus bundle=control
#pragma HLS INTERFACE s_axilite port=rdy_bus bundle=control
#pragma HLS INTERFACE s_axilite port=scores bundle=control
#pragma HLS INTERFACE s_axilite port=width bundle=control
#pragma HLS INTERFACE s_axilite port=height bundle=control
#pragma HLS INTERFACE s_axilite port=n_pts bundle=control
#pragma HLS INTERFACE s_axilite port=half_w bundle=control
#pragma HLS INTERFACE s_axilite port=half_h bundle=control
#pragma HLS INTERFACE s_axilite port=stride bundle=control
#pragma HLS INTERFACE s_axilite port=cycles bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control
    if (width != kChamferWidth || height != kChamferHeight || n_pts < 1 || n_pts > kChamferMaxPoints) {
        if (cycles != 0) {
            *cycles = 0;
        }
        return;
    }
    int16_t rdx[kChamferMaxPoints];
    int16_t rdy[kChamferMaxPoints];
#pragma HLS ARRAY_PARTITION variable=rdx cyclic factor=8
#pragma HLS ARRAY_PARTITION variable=rdy cyclic factor=8
    load_points(rdx_bus, n_pts, rdx);
    load_points(rdy_bus, n_pts, rdy);
    const int step = stride < 1 ? 1 : stride;
    const int y0 = half_h;
    const int y1 = kChamferHeight - half_h;
    const int x0 = half_w;
    const int x1 = kChamferWidth - half_w;
    int slot = 0;
    ap_uint<128> packed = 0;
    int lane = 0;
    for (int y = y0; y < y1; y += step) {
        for (int x = x0; x < x1; x += step) {
#pragma HLS PIPELINE II=8
#pragma HLS LOOP_FLATTEN
            uint16_t rowbuf[kChamferMaxPoints];
#pragma HLS ARRAY_PARTITION variable=rowbuf cyclic factor=8
            int acc = 0;
            int valid = 0;
            for (int i = 0; i < kChamferMaxPoints; i += 8) {
#pragma HLS UNROLL
                if (i < n_pts) {
                    for (int k = 0; k < 8; ++k) {
#pragma HLS UNROLL
                        if (i + k < n_pts) {
                            const int sx = x + rdx[i + k];
                            const int sy = y + rdy[i + k];
                            if (sx >= 0 && sy >= 0 && sx < kChamferWidth && sy < kChamferHeight) {
                                const int word = (sy * kChamferWidth + sx) / 8;
                                const int sub = (sy * kChamferWidth + sx) % 8;
                                const ap_uint<128> beat = distance[word];
                                rowbuf[i + k] = static_cast<uint16_t>(beat.range(sub * 16 + 15, sub * 16));
                                acc += rowbuf[i + k];
                                valid += 1;
                            }
                        }
                    }
                }
            }
            const int score = valid == 0 ? 0 : (valid * 256) / (valid + acc);
            packed.range(lane * 16 + 15, lane * 16) = score;
            lane += 1;
            if (lane == 8) {
                scores[slot] = packed;
                slot += 1;
                lane = 0;
                packed = 0;
            }
        }
    }
    if (lane != 0) {
        scores[slot] = packed;
    }
    if (cycles != 0) {
        const int nx = (x1 - x0 + step - 1) / step;
        const int ny = (y1 - y0 + step - 1) / step;
        *cycles = static_cast<uint32_t>(nx * ny * ((n_pts + 7) / 8));
    }
}
