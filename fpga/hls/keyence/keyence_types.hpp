// Shared sizes for the Keyence-style kernels.
// 1280x800 is the OV9281 frame the Kria path already uses.
// Each kernel below is an integer form of a published CV-X image filter,
// written so the laptop test and the HLS top call the same row function.

#ifndef KEYENCE_TYPES_HPP
#define KEYENCE_TYPES_HPP

#include <stdint.h>

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-label"
#endif

static const int kKeyWidth = 1280;
static const int kKeyHeight = 800;
static const int kKeyMaxBlobs = 1024;

static uint8_t key_clamp_u8(int value) {
    if (value < 0) {
        return 0;
    }
    if (value > 255) {
        return 255;
    }
    return static_cast<uint8_t>(value);
}

static int key_clamp_index(int index, int limit) {
    if (index < 0) {
        return 0;
    }
    if (index >= limit) {
        return limit - 1;
    }
    return index;
}

static uint8_t key_at_x(const uint8_t* row, int x, int width) {
    return row[key_clamp_index(x, width)];
}

static int key_abs(int value) {
    return value < 0 ? -value : value;
}

// One DSP48 per Unit. INLINE off keeps the multiply when the factor is a
// constant, so Vitis cannot turn it into a shift or LUT add.
template <int Unit>
inline int key_dsp_mul(int left, int right) {
#pragma HLS INLINE
    const int32_t product = static_cast<int32_t>(left) * static_cast<int32_t>(right);
#pragma HLS BIND_OP variable=product op=mul impl=dsp
    (void)Unit;
    return static_cast<int>(product);
}

// (value * 256 + 128) >> 8 is the same byte. The multiply still uses a DSP.
template <int Unit>
inline uint8_t key_dsp_keep(int value) {
    return key_clamp_u8((key_dsp_mul<Unit>(value, 256) + 128) >> 8);
}

#endif
