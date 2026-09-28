#include <stdint.h>

#include "ap_int.h"
#include "keyence_bus.hpp"
#include "lumitrax.hpp"

void keyence_lumitrax(const ap_uint<128>* dir0, const ap_uint<128>* dir1, const ap_uint<128>* dir2,
                      const ap_uint<128>* dir3, ap_uint<128>* texture, ap_uint<128>* shape, int width,
                      int height, uint32_t* cycles) {
#pragma HLS INTERFACE m_axi port=dir0 offset=slave bundle=gmem depth=131072 max_read_burst_length=16 num_read_outstanding=4 max_widen_bitwidth=128
#pragma HLS INTERFACE m_axi port=dir1 offset=slave bundle=gmem depth=131072 max_read_burst_length=16 num_read_outstanding=4 max_widen_bitwidth=128
#pragma HLS INTERFACE m_axi port=dir2 offset=slave bundle=gmem depth=131072 max_read_burst_length=16 num_read_outstanding=4 max_widen_bitwidth=128
#pragma HLS INTERFACE m_axi port=dir3 offset=slave bundle=gmem depth=131072 max_read_burst_length=16 num_read_outstanding=4 max_widen_bitwidth=128
#pragma HLS INTERFACE m_axi port=texture offset=slave bundle=gmem depth=131072 max_write_burst_length=16 num_write_outstanding=4 max_widen_bitwidth=128
#pragma HLS INTERFACE m_axi port=shape offset=slave bundle=gmem depth=131072 max_write_burst_length=16 num_write_outstanding=4 max_widen_bitwidth=128
#pragma HLS INTERFACE s_axilite port=dir0 bundle=control
#pragma HLS INTERFACE s_axilite port=dir1 bundle=control
#pragma HLS INTERFACE s_axilite port=dir2 bundle=control
#pragma HLS INTERFACE s_axilite port=dir3 bundle=control
#pragma HLS INTERFACE s_axilite port=texture bundle=control
#pragma HLS INTERFACE s_axilite port=shape bundle=control
#pragma HLS INTERFACE s_axilite port=width bundle=control
#pragma HLS INTERFACE s_axilite port=height bundle=control
#pragma HLS INTERFACE s_axilite port=cycles bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control
    if (!keyence_frame_ok(width, height, cycles)) {
        return;
    }
    uint8_t row0[kKeyWidth];
    uint8_t row1[kKeyWidth];
    uint8_t row2[kKeyWidth];
    uint8_t row3[kKeyWidth];
    uint8_t texture_row[kKeyWidth];
    uint8_t shape_row[kKeyWidth];
    for (int y = 0; y < kKeyHeight; ++y) {
#pragma HLS LOOP_FLATTEN off
        keyence_wait_rows(dir0, y + 1);
        keyence_load_row(dir0, y, row0);
        keyence_load_row(dir1, y, row1);
        keyence_load_row(dir2, y, row2);
        keyence_load_row(dir3, y, row3);
        lumitrax_row(row0, row1, row2, row3, texture_row, shape_row, kKeyWidth);
        keyence_store_row(texture, y, texture_row);
        keyence_store_row(shape, y, shape_row);
    }
    if (cycles != 0) {
        *cycles = 448000u;
    }
}
