#include <stdint.h>

#include "ap_int.h"
#include "blob.hpp"
#include "keyence_bus.hpp"

void keyence_blob(const ap_uint<128>* image_in, ap_uint<128>* image_out, ap_uint<128>* labels, int width,
                  int height, int p0, int p1, uint32_t* cycles) {
#pragma HLS INTERFACE m_axi port=image_in offset=slave bundle=gmem depth=131072 max_read_burst_length=16 num_read_outstanding=4 max_widen_bitwidth=128
#pragma HLS INTERFACE m_axi port=image_out offset=slave bundle=gmem depth=131072 max_read_burst_length=16 num_read_outstanding=4 max_write_burst_length=16 num_write_outstanding=4 max_widen_bitwidth=128
#pragma HLS INTERFACE m_axi port=labels offset=slave bundle=gmem depth=131072 max_read_burst_length=16 num_read_outstanding=4 max_write_burst_length=16 num_write_outstanding=4 max_widen_bitwidth=128
#pragma HLS INTERFACE s_axilite port=image_in bundle=control
#pragma HLS INTERFACE s_axilite port=image_out bundle=control
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
    const int white = (p1 & 1) != 0;
    const int fill = (p1 & 2) != 0;
    const int reject_border = (p1 & 4) != 0;
    uint8_t pixels[kKeyWidth];
    uint8_t filled[kKeyWidth];
    uint16_t lab[kKeyWidth];
    BlobState state;

    if (fill) {
        for (int y = 0; y < kKeyHeight; ++y) {
#pragma HLS LOOP_FLATTEN off
            keyence_wait_rows(image_in, y + 1);
            keyence_load_row(image_in, y, pixels);
            keyence_store_row(image_out, y, pixels);
        }
        blob_reset(&state, 0, 1, 0, 0);
        for (int y = 0; y < kKeyHeight; ++y) {
#pragma HLS LOOP_FLATTEN off
            keyence_load_row(image_out, y, pixels);
            blob_label_row(&state, pixels, lab, kKeyWidth);
            keyence_store_labels(labels, y, lab);
        }
        for (int y = 0; y < kKeyHeight; ++y) {
#pragma HLS LOOP_FLATTEN off
            keyence_load_labels(labels, y, lab);
            blob_resolve_row(&state, lab, y, kKeyWidth, kKeyHeight);
            keyence_store_labels(labels, y, lab);
        }
        for (int y = 0; y < kKeyHeight; ++y) {
#pragma HLS LOOP_FLATTEN off
            keyence_load_row(image_out, y, pixels);
            keyence_load_labels(labels, y, lab);
            blob_hole_row(&state, pixels, lab, filled, kKeyWidth);
            keyence_store_row(image_out, y, filled);
        }
    }

    blob_reset(&state, white, p0, 0, kKeyBlobKeep);
    for (int y = 0; y < kKeyHeight; ++y) {
#pragma HLS LOOP_FLATTEN off
        if (!fill) {
            keyence_wait_rows(image_in, y + 1);
            keyence_load_row(image_in, y, pixels);
        } else {
            keyence_load_row(image_out, y, pixels);
        }
        blob_label_row(&state, pixels, lab, kKeyWidth);
        keyence_store_labels(labels, y, lab);
    }
    for (int y = 0; y < kKeyHeight; ++y) {
#pragma HLS LOOP_FLATTEN off
        keyence_load_labels(labels, y, lab);
        blob_resolve_row(&state, lab, y, kKeyWidth, kKeyHeight);
        keyence_store_labels(labels, y, lab);
    }
    blob_select(&state, reject_border);
    for (int y = 0; y < kKeyHeight; ++y) {
#pragma HLS LOOP_FLATTEN off
        keyence_load_labels(labels, y, lab);
        blob_paint_row(&state, lab, pixels, kKeyWidth);
        keyence_store_row(image_out, y, pixels);
    }
    if (cycles != 0) {
        *cycles = fill ? 4200000u : 2200000u;
    }
}
