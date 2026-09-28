#include <stdint.h>

#include "ap_int.h"
#include "keyence_bus.hpp"
#include "preserve_intensity.hpp"

void keyence_preserve_intensity(const ap_uint<128>* image_in, const ap_uint<128>* image_ref,
                                ap_uint<128>* image_out, int width, int height, uint32_t* cycles) {
#pragma HLS INTERFACE m_axi port=image_in offset=slave bundle=gmem depth=131072 max_read_burst_length=16 num_read_outstanding=4 max_widen_bitwidth=128
#pragma HLS INTERFACE m_axi port=image_ref offset=slave bundle=gmem depth=131072 max_read_burst_length=16 num_read_outstanding=4 max_widen_bitwidth=128
#pragma HLS INTERFACE m_axi port=image_out offset=slave bundle=gmem depth=131072 max_write_burst_length=16 num_write_outstanding=4 max_widen_bitwidth=128
#pragma HLS INTERFACE s_axilite port=image_in bundle=control
#pragma HLS INTERFACE s_axilite port=image_ref bundle=control
#pragma HLS INTERFACE s_axilite port=image_out bundle=control
#pragma HLS INTERFACE s_axilite port=width bundle=control
#pragma HLS INTERFACE s_axilite port=height bundle=control
#pragma HLS INTERFACE s_axilite port=cycles bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control
    if (!keyence_frame_ok(width, height, cycles)) {
        return;
    }
    uint8_t current[kKeyWidth];
    uint8_t reference[kKeyWidth];
    uint8_t dst[kKeyWidth];
    uint32_t sum_cur = 0;
    uint32_t sum_ref = 0;
    for (int y = 0; y < kKeyHeight; ++y) {
#pragma HLS LOOP_FLATTEN off
        keyence_wait_rows(image_in, y + 1);
        keyence_load_row(image_in, y, current);
        keyence_load_row(image_ref, y, reference);
        preserve_sum_row(current, kKeyWidth, &sum_cur);
        preserve_sum_row(reference, kKeyWidth, &sum_ref);
    }
    const int gain = preserve_gain_q8(sum_ref, sum_cur);
    for (int y = 0; y < kKeyHeight; ++y) {
#pragma HLS LOOP_FLATTEN off
        keyence_load_row(image_in, y, current);
        for (int x = 0; x < kKeyWidth; ++x) {
#pragma HLS PIPELINE II=1
            dst[x] = preserve_pixel(current[x], gain);
        }
        keyence_store_row(image_out, y, dst);
    }
    if (cycles != 0) {
        *cycles = 448000u;
    }
}
