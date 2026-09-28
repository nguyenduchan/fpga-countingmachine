// Blob filter. 4-connected labels, area limits, optional hole fill and border reject.
// Output is a mask: 255 where the blob is kept.
// flags bit 0: white objects (1) or black objects (0).
// flags bit 1: fill holes before labeling.
// flags bit 2: drop blobs that touch the frame.
// At most kKeyBlobKeep blobs are kept, the largest ones.
// Label ids stop at kKeyMaxBlobs. Extra objects share the last id.

#ifndef KEYENCE_BLOB_HPP
#define KEYENCE_BLOB_HPP

#include "keyence_scan.hpp"

static const int kKeyBlobKeep = 64;

struct BlobState {
    int parent[kKeyMaxBlobs];
    int area[kKeyMaxBlobs];
    uint8_t touch[kKeyMaxBlobs];
    uint8_t keep[kKeyMaxBlobs];
    int next_id;
    int min_area;
    int max_area;
    int max_keep;
    int white;
    int prev[kKeyWidth];
    int curr[kKeyWidth];
};

inline void blob_reset(BlobState* state, int white, int min_area, int max_area, int max_keep) {
    for (int id = 0; id < kKeyMaxBlobs; ++id) {
        state->parent[id] = id;
        state->area[id] = 0;
        state->touch[id] = 0;
        state->keep[id] = 0;
    }
    state->next_id = 1;
    state->min_area = min_area;
    state->max_area = max_area;
    state->max_keep = max_keep;
    state->white = white;
    for (int x = 0; x < kKeyWidth; ++x) {
        state->prev[x] = 0;
        state->curr[x] = 0;
    }
}

inline int blob_find(BlobState* state, int id) {
    int root = id;
    while (state->parent[root] != root) {
        root = state->parent[root];
    }
    while (id != root) {
        const int next = state->parent[id];
        state->parent[id] = root;
        id = next;
    }
    return root;
}

inline void blob_unite(BlobState* state, int left, int up) {
    left = blob_find(state, left);
    up = blob_find(state, up);
    if (left == 0 || up == 0 || left == up) {
        return;
    }
    if (up < left) {
        const int swap = left;
        left = up;
        up = swap;
    }
    state->parent[up] = left;
}

inline int blob_foreground(const BlobState* state, uint8_t pixel) {
    if (state->white) {
        return pixel != 0;
    }
    return pixel == 0;
}

inline void blob_label_row(BlobState* state, const uint8_t* pixels, uint16_t* labels, int width) {
    for (int x = 0; x < width; ++x) {
        int id = 0;
        if (blob_foreground(state, pixels[x])) {
            const int left = (x > 0) ? state->curr[x - 1] : 0;
            const int up = state->prev[x];
            if (left == 0 && up == 0) {
                if (state->next_id < kKeyMaxBlobs) {
                    id = state->next_id;
                    state->next_id += 1;
                } else {
                    id = kKeyMaxBlobs - 1;
                }
            } else if (left == 0) {
                id = up;
            } else if (up == 0) {
                id = left;
            } else {
                blob_unite(state, left, up);
                id = left;
            }
        }
        state->curr[x] = id;
        labels[x] = static_cast<uint16_t>(id);
    }
    for (int x = 0; x < width; ++x) {
        state->prev[x] = state->curr[x];
    }
}

inline void blob_resolve_row(BlobState* state, uint16_t* labels, int y, int width, int height) {
    for (int x = 0; x < width; ++x) {
        const int id = labels[x];
        if (id == 0) {
            continue;
        }
        const int root = blob_find(state, id);
        labels[x] = static_cast<uint16_t>(root);
        state->area[root] += 1;
        if (x == 0 || y == 0 || x == width - 1 || y == height - 1) {
            state->touch[root] = 1;
        }
    }
}

inline void blob_select(BlobState* state, int reject_border) {
    int ids[kKeyMaxBlobs];
    int count = 0;
    for (int id = 1; id < kKeyMaxBlobs; ++id) {
        if (state->parent[id] != id || state->area[id] <= 0) {
            continue;
        }
        if (state->area[id] < state->min_area) {
            continue;
        }
        if (state->max_area > 0 && state->area[id] > state->max_area) {
            continue;
        }
        if (reject_border && state->touch[id]) {
            continue;
        }
        ids[count] = id;
        count += 1;
    }
    const int limit = (state->max_keep <= 0 || state->max_keep > count) ? count : state->max_keep;
    for (int rank = 0; rank < limit; ++rank) {
        int best = rank;
        for (int index = rank + 1; index < count; ++index) {
            if (state->area[ids[index]] > state->area[ids[best]]) {
                best = index;
            }
        }
        const int swap = ids[rank];
        ids[rank] = ids[best];
        ids[best] = swap;
        state->keep[ids[rank]] = 1;
    }
}

inline void blob_paint_row(const BlobState* state, const uint16_t* labels, uint8_t* dst, int width) {
    for (int x0 = 0; x0 < width; x0 += 16) {
#pragma HLS PIPELINE II=1
        for (int lane = 0; lane < 16; ++lane) {
#pragma HLS UNROLL
            const int x = x0 + lane;
            if (x < width) {
                const int id = labels[x];
                dst[x] = key_dsp_keep<8>((id > 0 && state->keep[id]) ? 255 : 0);
            }
        }
    }
}

inline void blob_hole_row(const BlobState* state, const uint8_t* pixels, const uint16_t* labels,
                          uint8_t* dst, int width) {
    for (int x0 = 0; x0 < width; x0 += 16) {
#pragma HLS PIPELINE II=1
        for (int lane = 0; lane < 16; ++lane) {
#pragma HLS UNROLL
            const int x = x0 + lane;
            if (x < width) {
                const int id = labels[x];
                if (pixels[x] == 0 && id != 0 && state->touch[id] == 0) {
                    dst[x] = 255;
                } else {
                    dst[x] = pixels[x];
                }
            }
        }
    }
}

#ifndef __SYNTHESIS__
inline void apply_blob(const uint8_t* image, uint8_t* out, uint16_t* labels, int width, int height,
                       int min_area, int max_area, int flags) {
    if (width <= 0 || height <= 0 || width > kKeyWidth || height > kKeyHeight) {
        return;
    }
    const int white = (flags & 1) != 0;
    const int fill = (flags & 2) != 0;
    const int reject_border = (flags & 4) != 0;
    const uint8_t* source = image;
    if (fill) {
        const int count = width * height;
        for (int index = 0; index < count; ++index) {
            out[index] = image[index];
        }
        BlobState holes;
        blob_reset(&holes, 0, 1, 0, 0);
        for (int y = 0; y < height; ++y) {
            blob_label_row(&holes, out + y * width, labels + y * width, width);
        }
        for (int y = 0; y < height; ++y) {
            blob_resolve_row(&holes, labels + y * width, y, width, height);
        }
        for (int y = 0; y < height; ++y) {
            blob_hole_row(&holes, out + y * width, labels + y * width, out + y * width, width);
        }
        source = out;
    }

    BlobState state;
    blob_reset(&state, white, min_area, max_area, kKeyBlobKeep);
    for (int y = 0; y < height; ++y) {
        blob_label_row(&state, source + y * width, labels + y * width, width);
    }
    for (int y = 0; y < height; ++y) {
        blob_resolve_row(&state, labels + y * width, y, width, height);
    }
    blob_select(&state, reject_border);
    for (int y = 0; y < height; ++y) {
        blob_paint_row(&state, labels + y * width, out + y * width, width);
    }
}
#endif

#endif
