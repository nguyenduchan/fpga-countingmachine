// Chamfer match from yolo_vision (chamfer_center_score).
// Score at a center is valid / (valid + sum of distance-transform samples).
// The distance field is the 3-4 chamfer metric, which streams on the FPGA:
// one pixel per cycle forward, then one pixel per cycle backward.
// The CPU and the HLS kernel call these same row functions.

#ifndef CHAMFER_CORE_HPP
#define CHAMFER_CORE_HPP

#include <stdint.h>

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#endif

static const int kChamferWidth = 1280;
static const int kChamferHeight = 800;
static const int kChamferMaxPoints = 128;
static const int kChamferInf = 3000;
static const int kChamferOrth = 3;
static const int kChamferDiag = 4;

inline int chamfer_edge_pixel(const uint8_t* up, const uint8_t* mid, const uint8_t* down, int x, int width,
                              int thresh) {
    (void)down;
    (void)width;
    const int xm = x > 0 ? x - 1 : x;
    int gx = static_cast<int>(mid[x]) - static_cast<int>(mid[xm]);
    if (gx < 0) {
        gx = -gx;
    }
    int gy = static_cast<int>(mid[x]) - static_cast<int>(up[x]);
    if (gy < 0) {
        gy = -gy;
    }
    return (gx + gy) >= thresh ? 1 : 0;
}

inline void chamfer_forward_row(const uint8_t* edge, const uint16_t* prev, uint16_t* cur, int width, int have_prev) {
    int left = kChamferInf;
    for (int x = 0; x < width; ++x) {
#pragma HLS PIPELINE II=1
        int value = edge[x] ? 0 : kChamferInf;
        if (have_prev) {
            const int xl = x > 0 ? x - 1 : 0;
            const int xr = x + 1 < width ? x + 1 : width - 1;
            const int up_l = static_cast<int>(prev[xl]) + kChamferDiag;
            const int up_c = static_cast<int>(prev[x]) + kChamferOrth;
            const int up_r = static_cast<int>(prev[xr]) + kChamferDiag;
            if (up_l < value) {
                value = up_l;
            }
            if (up_c < value) {
                value = up_c;
            }
            if (up_r < value) {
                value = up_r;
            }
        }
        const int from_left = left + kChamferOrth;
        if (from_left < value) {
            value = from_left;
        }
        if (value > kChamferInf) {
            value = kChamferInf;
        }
        cur[x] = static_cast<uint16_t>(value);
        left = value;
    }
}

inline void chamfer_backward_row(uint16_t* cur, const uint16_t* below, int width, int have_below) {
    int right = kChamferInf;
    for (int x = width - 1; x >= 0; --x) {
#pragma HLS PIPELINE II=1
        int value = cur[x];
        const int from_right = right + kChamferOrth;
        if (from_right < value) {
            value = from_right;
        }
        if (have_below) {
            const int xl = x > 0 ? x - 1 : 0;
            const int xr = x + 1 < width ? x + 1 : width - 1;
            const int dn_l = static_cast<int>(below[xl]) + kChamferDiag;
            const int dn_c = static_cast<int>(below[x]) + kChamferOrth;
            const int dn_r = static_cast<int>(below[xr]) + kChamferDiag;
            if (dn_l < value) {
                value = dn_l;
            }
            if (dn_c < value) {
                value = dn_c;
            }
            if (dn_r < value) {
                value = dn_r;
            }
        }
        if (value > kChamferInf) {
            value = kChamferInf;
        }
        cur[x] = static_cast<uint16_t>(value);
        right = value;
    }
}

// Q8 of 1 / (1 + mean distance). 256 means every sample sat on an edge.
inline int chamfer_score_q8(const uint16_t* dt, int width, int height, int cx, int cy, const int16_t* rdx,
                            const int16_t* rdy, int n_pts) {
    int acc = 0;
    int valid = 0;
    const int used = n_pts < kChamferMaxPoints ? n_pts : kChamferMaxPoints;
    for (int i = 0; i < used; ++i) {
#pragma HLS PIPELINE II=1
        const int x = cx + rdx[i];
        const int y = cy + rdy[i];
        if (x >= 0 && y >= 0 && x < width && y < height) {
            acc += dt[y * width + x];
            valid += 1;
        }
    }
    if (valid == 0) {
        return 0;
    }
    return (valid * 256) / (valid + acc);
}

#ifndef __SYNTHESIS__

inline void chamfer_from_edges(const uint8_t* edge, uint16_t* dt, int width, int height) {
    uint16_t prev[kChamferWidth];
    uint16_t cur[kChamferWidth];
    for (int y = 0; y < height; ++y) {
        chamfer_forward_row(edge + y * width, prev, cur, width, y > 0);
        for (int x = 0; x < width; ++x) {
            dt[y * width + x] = cur[x];
            prev[x] = cur[x];
        }
    }
    for (int y = height - 1; y >= 0; --y) {
        for (int x = 0; x < width; ++x) {
            cur[x] = dt[y * width + x];
        }
        const uint16_t* below = (y + 1 < height) ? dt + (y + 1) * width : 0;
        chamfer_backward_row(cur, below, width, y + 1 < height);
        for (int x = 0; x < width; ++x) {
            dt[y * width + x] = cur[x];
        }
    }
}

inline void chamfer_distance(const uint8_t* gray, uint16_t* dt, int width, int height, int thresh) {
    uint8_t up[kChamferWidth];
    uint8_t mid[kChamferWidth];
    uint8_t down[kChamferWidth];
    uint8_t edge[kChamferWidth];
    uint16_t prev[kChamferWidth];
    uint16_t cur[kChamferWidth];
    for (int x = 0; x < width; ++x) {
        up[x] = gray[x];
        mid[x] = gray[x];
    }
    for (int y = 0; y < height; ++y) {
        const int yb = y + 1 < height ? y + 1 : y;
        int edge_px[kChamferWidth];
        for (int x = 0; x < width; ++x) {
            down[x] = gray[yb * width + x];
            edge_px[x] = chamfer_edge_pixel(up, mid, down, x, width, thresh);
        }
        uint8_t edge_row[kChamferWidth];
        for (int x = 0; x < width; ++x) {
            edge_row[x] = static_cast<uint8_t>(edge_px[x]);
        }
        chamfer_forward_row(edge_row, prev, cur, width, y > 0);
        for (int x = 0; x < width; ++x) {
            dt[y * width + x] = cur[x];
            prev[x] = cur[x];
            up[x] = mid[x];
            mid[x] = down[x];
        }
    }
    for (int y = height - 1; y >= 0; --y) {
        for (int x = 0; x < width; ++x) {
            cur[x] = dt[y * width + x];
        }
        const uint16_t* below = (y + 1 < height) ? dt + (y + 1) * width : 0;
        chamfer_backward_row(cur, below, width, y + 1 < height);
        for (int x = 0; x < width; ++x) {
            dt[y * width + x] = cur[x];
        }
    }
}

struct ChamferPeak {
    int x;
    int y;
    int score_q8;
};

// Same search shape as yolo_vision chamfer_sparse_match_peaks:
// stride grid, keep strong centers, refine a small window, then suppress neighbors.
inline int chamfer_match(const uint16_t* dt, int width, int height, const int16_t* rdx, const int16_t* rdy,
                         int n_pts, int half_w, int half_h, int min_score_q8, int stride, int suppress_r,
                         int refine_radius, int max_peaks, ChamferPeak* peaks) {
    if (max_peaks < 1 || n_pts < 1) {
        return 0;
    }
    const int step = stride < 1 ? 1 : stride;
    const int y0 = half_h;
    const int y1 = height - half_h;
    const int x0 = half_w;
    const int x1 = width - half_w;
    if (y1 <= y0 || x1 <= x0) {
        return 0;
    }
    const int coarse_min = min_score_q8 * 85 / 100;
    const int cap = 2048;
    int cx[2048];
    int cy[2048];
    int cs[2048];
    int coarse_n = 0;
    for (int y = y0; y < y1; y += step) {
        for (int x = x0; x < x1; x += step) {
            const int sc = chamfer_score_q8(dt, width, height, x, y, rdx, rdy, n_pts);
            if (sc >= coarse_min && coarse_n < cap) {
                cx[coarse_n] = x;
                cy[coarse_n] = y;
                cs[coarse_n] = sc;
                coarse_n += 1;
            }
        }
    }
    for (int i = 1; i < coarse_n; ++i) {
        const int sx = cx[i];
        const int sy = cy[i];
        const int ss = cs[i];
        int j = i;
        while (j > 0 && cs[j - 1] < ss) {
            cx[j] = cx[j - 1];
            cy[j] = cy[j - 1];
            cs[j] = cs[j - 1];
            j -= 1;
        }
        cx[j] = sx;
        cy[j] = sy;
        cs[j] = ss;
    }
    const int keep = coarse_n < (max_peaks * 8 > 64 ? max_peaks * 8 : 64) ? coarse_n
                                                                            : (max_peaks * 8 > 64 ? max_peaks * 8 : 64);
    const int rad = refine_radius < 1 ? 1 : refine_radius;
    int peak_n = 0;
    for (int i = 0; i < keep; ++i) {
        int best_x = cx[i];
        int best_y = cy[i];
        int best = chamfer_score_q8(dt, width, height, best_x, best_y, rdx, rdy, n_pts);
        for (int dy = -rad; dy <= rad; ++dy) {
            for (int dx = -rad; dx <= rad; ++dx) {
                const int nx = cx[i] + dx;
                const int ny = cy[i] + dy;
                if (nx < x0 || nx >= x1 || ny < y0 || ny >= y1) {
                    continue;
                }
                const int sc = chamfer_score_q8(dt, width, height, nx, ny, rdx, rdy, n_pts);
                if (sc > best) {
                    best = sc;
                    best_x = nx;
                    best_y = ny;
                }
            }
        }
        if (best < min_score_q8) {
            continue;
        }
        int blocked = 0;
        for (int p = 0; p < peak_n; ++p) {
            const int ddx = best_x - peaks[p].x;
            const int ddy = best_y - peaks[p].y;
            if (ddx * ddx + ddy * ddy < suppress_r * suppress_r) {
                blocked = 1;
                break;
            }
        }
        if (blocked || peak_n >= max_peaks) {
            continue;
        }
        peaks[peak_n].x = best_x;
        peaks[peak_n].y = best_y;
        peaks[peak_n].score_q8 = best;
        peak_n += 1;
    }
    return peak_n;
}

#endif

#endif
