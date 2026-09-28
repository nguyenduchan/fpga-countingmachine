#include <stdint.h>

#include "ap_int.h"
#include "keyence_bus.hpp"
#include "noise_isolation.hpp"

void keyence_noise_isolation(const ap_uint<128>* image_in, ap_uint<128>* image_out, ap_uint<128>* scratch,
                             ap_uint<128>* labels, int width, int height, int p0, int p1, uint32_t* cycles) {
#pragma HLS INTERFACE m_axi port=image_in offset=slave bundle=gmem depth=131072 max_read_burst_length=16 num_read_outstanding=4 max_widen_bitwidth=128
#pragma HLS INTERFACE m_axi port=image_out offset=slave bundle=gmem depth=131072 max_read_burst_length=16 num_read_outstanding=4 max_write_burst_length=16 num_write_outstanding=4 max_widen_bitwidth=128
#pragma HLS INTERFACE m_axi port=scratch offset=slave bundle=gmem depth=131072 max_read_burst_length=16 num_read_outstanding=4 max_write_burst_length=16 num_write_outstanding=4 max_widen_bitwidth=128
#pragma HLS INTERFACE m_axi port=labels offset=slave bundle=gmem depth=131072 max_read_burst_length=16 num_read_outstanding=4 max_write_burst_length=16 num_write_outstanding=4 max_widen_bitwidth=128
#pragma HLS INTERFACE s_axilite port=image_in bundle=control
#pragma HLS INTERFACE s_axilite port=image_out bundle=control
#pragma HLS INTERFACE s_axilite port=scratch bundle=control
#pragma HLS INTERFACE s_axilite port=labels bundle=control
#pragma HLS INTERFACE s_axilite port=width bundle=control
#pragma HLS INTERFACE s_axilite port=height bundle=control
#pragma HLS INTERFACE s_axilite port=p0 bundle=control
#pragma HLS INTERFACE s_axilite port=p1 bundle=control
#pragma HLS INTERFACE s_axilite port=cycles bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control
    if (!keyence_frame_ok(width, height, cycles)) {
        return;
    }
    uint8_t row0[kKeyWidth];
    uint8_t row1[kKeyWidth];
    uint8_t row2[kKeyWidth];
    uint8_t mask[kKeyWidth];
    uint8_t median[kKeyWidth];
    uint8_t dst[kKeyWidth];
    uint16_t lab[kKeyWidth];
    BlobState state;

    for (int y = 0; y < kKeyHeight; ++y) {
#pragma HLS LOOP_FLATTEN off
        keyence_wait_rows(image_in, y + 1);
        keyence_load_row(image_in, y, row0);
        noise_threshold_row(row0, dst, kKeyWidth);
        keyence_store_row(scratch, y, dst);
    }

    blob_reset(&state, 1, 1, p0, kKeyBlobKeep);
    for (int y = 0; y < kKeyHeight; ++y) {
#pragma HLS LOOP_FLATTEN off
        keyence_load_row(scratch, y, row0);
        blob_label_row(&state, row0, lab, kKeyWidth);
        keyence_store_labels(labels, y, lab);
    }
    for (int y = 0; y < kKeyHeight; ++y) {
#pragma HLS LOOP_FLATTEN off
        keyence_load_labels(labels, y, lab);
        blob_resolve_row(&state, lab, y, kKeyWidth, kKeyHeight);
        keyence_store_labels(labels, y, lab);
    }
    blob_select(&state, 0);
    for (int y = 0; y < kKeyHeight; ++y) {
#pragma HLS LOOP_FLATTEN off
        keyence_load_labels(labels, y, lab);
        blob_paint_row(&state, lab, mask, kKeyWidth);
        keyence_store_row(image_out, y, mask);
    }
    if (p1 != 0) {
        if (cycles != 0) {
            *cycles = 2400000u;
        }
        return;
    }

    keyence_load_row(image_in, 0, row0);
    for (int x = 0; x < kKeyWidth; ++x) {
#pragma HLS PIPELINE II=1
        row1[x] = row0[x];
    }
    for (int y = 0; y < kKeyHeight; ++y) {
#pragma HLS LOOP_FLATTEN off
        if (y + 1 < kKeyHeight) {
            keyence_load_row(image_in, y + 1, row2);
        } else {
            for (int x = 0; x < kKeyWidth; ++x) {
#pragma HLS PIPELINE II=1
                row2[x] = row1[x];
            }
        }
        keyence_emit3<MedianTag>(row0, row1, row2, median, kKeyWidth, 0, 0);
        keyence_load_row(image_out, y, mask);
        noise_combine_row(row1, mask, median, dst, kKeyWidth);
        keyence_store_row(scratch, y, dst);
        for (int x = 0; x < kKeyWidth; ++x) {
#pragma HLS PIPELINE II=1
            row0[x] = row1[x];
            row1[x] = row2[x];
        }
    }
    for (int y = 0; y < kKeyHeight; ++y) {
#pragma HLS LOOP_FLATTEN off
        keyence_load_row(scratch, y, dst);
        keyence_store_row(image_out, y, dst);
    }
    if (cycles != 0) {
        *cycles = 2800000u;
    }
}
