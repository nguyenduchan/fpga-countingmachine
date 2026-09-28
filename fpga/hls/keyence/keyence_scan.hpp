// Row kernels (keyence_emit*) are the FPGA arithmetic.
// keyence_apply* is the laptop driver around those same kernels.

#ifndef KEYENCE_SCAN_HPP
#define KEYENCE_SCAN_HPP

#include "keyence_types.hpp"

template <typename Tag>
void keyence_emit3(const uint8_t* row0, const uint8_t* row1, const uint8_t* row2,
                   uint8_t* dst, int width, int p0, int p1);

template <typename Tag>
void keyence_emit1(const uint8_t* src, uint8_t* dst, int width, int p0, int p1);

template <typename Tag>
void keyence_emit2(const uint8_t* current, const uint8_t* other, uint8_t* dst,
                   int width, int p0, int p1);

#ifndef __SYNTHESIS__

static void key_copy_row(const uint8_t* image, int width, int height, int y, uint8_t* dst) {
    const int src_y = key_clamp_index(y, height);
    const uint8_t* src = image + static_cast<int>(src_y * width);
    for (int x = 0; x < width; ++x) {
        dst[x] = src[x];
    }
}

static void key_store_row(uint8_t* image, int width, int y, const uint8_t* src) {
    uint8_t* dst = image + static_cast<int>(y * width);
    for (int x = 0; x < width; ++x) {
        dst[x] = src[x];
    }
}

static int key_size_ok(int width, int height) {
    return width > 0 && height > 0 && width <= kKeyWidth && height <= kKeyHeight;
}

template <typename Tag>
void keyence_apply3(const uint8_t* image, uint8_t* out, int width, int height, int p0, int p1) {
    if (!key_size_ok(width, height)) {
        return;
    }
    uint8_t row0[kKeyWidth];
    uint8_t row1[kKeyWidth];
    uint8_t row2[kKeyWidth];
    uint8_t dst[kKeyWidth];
    key_copy_row(image, width, height, 0, row0);
    key_copy_row(image, width, height, 0, row1);
    for (int y = 0; y < height; ++y) {
        key_copy_row(image, width, height, y + 1, row2);
        keyence_emit3<Tag>(row0, row1, row2, dst, width, p0, p1);
        key_store_row(out, width, y, dst);
        for (int x = 0; x < width; ++x) {
            row0[x] = row1[x];
            row1[x] = row2[x];
        }
    }
}

template <typename Tag>
void keyence_apply1(const uint8_t* image, uint8_t* out, int width, int height, int p0, int p1) {
    if (!key_size_ok(width, height)) {
        return;
    }
    uint8_t src[kKeyWidth];
    uint8_t dst[kKeyWidth];
    for (int y = 0; y < height; ++y) {
        key_copy_row(image, width, height, y, src);
        keyence_emit1<Tag>(src, dst, width, p0, p1);
        key_store_row(out, width, y, dst);
    }
}

template <typename Tag>
void keyence_apply2(const uint8_t* current, const uint8_t* other, uint8_t* out,
                    int width, int height, int p0, int p1) {
    if (!key_size_ok(width, height)) {
        return;
    }
    uint8_t row_a[kKeyWidth];
    uint8_t row_b[kKeyWidth];
    uint8_t dst[kKeyWidth];
    for (int y = 0; y < height; ++y) {
        key_copy_row(current, width, height, y, row_a);
        key_copy_row(other, width, height, y, row_b);
        keyence_emit2<Tag>(row_a, row_b, dst, width, p0, p1);
        key_store_row(out, width, y, dst);
    }
}

#endif

#endif
