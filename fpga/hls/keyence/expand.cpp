#include <stdint.h>

#include "ap_int.h"
#include "expand.hpp"
#include "keyence_bus.hpp"

void keyence_expand(const ap_uint<128>* image_in, ap_uint<128>* image_out, int width, int height, int p0, int p1,
                    uint32_t* cycles) {
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
    keyence_hls_window<ExpandTag>(image_in, image_out, width, height, p0, p1, 1, cycles);
}
