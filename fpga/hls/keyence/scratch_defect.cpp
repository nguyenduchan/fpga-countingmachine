#include <stdint.h>

#include "ap_int.h"
#include "keyence_bus.hpp"
#include "scratch_defect.hpp"

void keyence_scratch_defect(const ap_uint<128>* image_in, ap_uint<128>* image_out, int width, int height,
                            uint32_t* cycles) {
#pragma HLS INTERFACE m_axi port=image_in offset=slave bundle=gmem depth=131072 max_read_burst_length=16 num_read_outstanding=4 max_widen_bitwidth=128
#pragma HLS INTERFACE m_axi port=image_out offset=slave bundle=gmem depth=131072 max_write_burst_length=16 num_write_outstanding=4 max_widen_bitwidth=128
#pragma HLS INTERFACE s_axilite port=image_in bundle=control
#pragma HLS INTERFACE s_axilite port=image_out bundle=control
#pragma HLS INTERFACE s_axilite port=width bundle=control
#pragma HLS INTERFACE s_axilite port=height bundle=control
#pragma HLS INTERFACE s_axilite port=cycles bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control
    if (!keyence_frame_ok(width, height, cycles)) {
        return;
    }
    uint8_t rows[kScratchLines][kKeyWidth];
    uint8_t dst[kKeyWidth];
#pragma HLS BIND_STORAGE variable=rows type=ram_t2p impl=bram
    const int half = kScratchLines / 2;
    for (int line = 0; line < kScratchLines; ++line) {
        const int src_y = key_clamp_index(line - half, kKeyHeight);
        keyence_wait_rows(image_in, src_y + 1);
        keyence_load_row(image_in, src_y, rows[line]);
    }
    int base = 0;
    for (int y = 0; y < kKeyHeight; ++y) {
#pragma HLS LOOP_FLATTEN off
        scratch_emit(rows, kKeyWidth, dst, base);
        keyence_store_row(image_out, y, dst);
        if (y + 1 < kKeyHeight) {
            const int newest = key_clamp_index(y + 1 + half, kKeyHeight);
            keyence_wait_rows(image_in, newest + 1);
            keyence_load_row(image_in, newest, rows[base]);
            base += 1;
            if (base == kScratchLines) {
                base = 0;
            }
        }
    }
    if (cycles != 0) {
        *cycles = 384000u;
    }
}
