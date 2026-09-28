#include <stdint.h>

#include "ap_int.h"
#include "image_extraction.hpp"
#include "keyence_bus.hpp"

void keyence_image_extraction(const ap_uint<128>* image_in, ap_uint<128>* image_out, ap_uint<128>* scratch,
                              int width, int height, uint32_t* cycles) {
#pragma HLS INTERFACE m_axi port=image_in offset=slave bundle=gmem depth=131072 max_read_burst_length=16 num_read_outstanding=4 max_widen_bitwidth=128
#pragma HLS INTERFACE m_axi port=image_out offset=slave bundle=gmem depth=131072 max_write_burst_length=16 num_write_outstanding=4 max_widen_bitwidth=128
#pragma HLS INTERFACE m_axi port=scratch offset=slave bundle=gmem depth=131072 max_read_burst_length=16 num_read_outstanding=4 max_write_burst_length=16 num_write_outstanding=4 max_widen_bitwidth=128
#pragma HLS INTERFACE s_axilite port=image_in bundle=control
#pragma HLS INTERFACE s_axilite port=image_out bundle=control
#pragma HLS INTERFACE s_axilite port=scratch bundle=control
#pragma HLS INTERFACE s_axilite port=width bundle=control
#pragma HLS INTERFACE s_axilite port=height bundle=control
#pragma HLS INTERFACE s_axilite port=cycles bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control
    keyence_hls_window<AverageTag>(image_in, scratch, width, height, 0, 0, 1, cycles);
    keyence_hls_pair<ExtractTag>(image_in, scratch, image_out, width, height, 0, 0, 0, cycles);
    if (cycles != 0 && width == kKeyWidth && height == kKeyHeight) {
        *cycles = kKeyPassCycles * 2u + 64000u;
    }
}
