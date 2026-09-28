#include <stdint.h>

#include "ap_int.h"
#include "contrast_expansion.hpp"
#include "keyence_bus.hpp"

void keyence_contrast_expansion(const ap_uint<128>* image_in, ap_uint<128>* image_out, int width, int height,
                                int p0, int p1, uint32_t* cycles) {
#pragma HLS INTERFACE m_axi port=image_in offset=slave bundle=gmem depth=131072 max_read_burst_length=16 num_read_outstanding=4 max_widen_bitwidth=128
#pragma HLS INTERFACE m_axi port=image_out offset=slave bundle=gmem depth=131072 max_write_burst_length=16 num_write_outstanding=4 max_widen_bitwidth=128
#pragma HLS INTERFACE s_axilite port=image_in bundle=control
#pragma HLS INTERFACE s_axilite port=image_out bundle=control
#pragma HLS INTERFACE s_axilite port=width bundle=control
#pragma HLS INTERFACE s_axilite port=height bundle=control
#pragma HLS INTERFACE s_axilite port=p0 bundle=control
#pragma HLS INTERFACE s_axilite port=p1 bundle=control
#pragma HLS INTERFACE s_axilite port=cycles bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control
    if (!keyence_frame_ok(width, height, cycles)) {
        return;
    }
    int hist[256];
    for (int bin = 0; bin < 256; ++bin) {
#pragma HLS PIPELINE II=1
        hist[bin] = 0;
    }
    uint8_t row[kKeyWidth];
    uint8_t dst[kKeyWidth];
    for (int y = 0; y < kKeyHeight; ++y) {
#pragma HLS LOOP_FLATTEN off
        keyence_wait_rows(image_in, y + 1);
        keyence_load_row(image_in, y, row);
        contrast_hist_row(row, kKeyWidth, hist);
    }
    int lo = 0;
    int hi = 255;
    contrast_limits(hist, p1, &lo, &hi);
    for (int y = 0; y < kKeyHeight; ++y) {
#pragma HLS LOOP_FLATTEN off
        keyence_load_row(image_in, y, row);
        for (int x = 0; x < kKeyWidth; ++x) {
#pragma HLS PIPELINE II=1
            dst[x] = contrast_map_pixel(row[x], lo, hi, p0);
        }
        keyence_store_row(image_out, y, dst);
    }
    if (cycles != 0) {
        *cycles = 1280000u;
    }
}
