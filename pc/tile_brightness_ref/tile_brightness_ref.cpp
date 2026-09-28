// Program 2: FPGA stand-in. Same source as the HLS kernel.
// The arithmetic is fpga/hls/tile_brightness_core.hpp, the same file the
// Vitis HLS kernel includes.
//
// Build:
//   g++ -O2 -std=c++17 -Wno-unknown-pragmas tile_brightness_ref.cpp -o tile_brightness_ref
//
//   tile_brightness_ref --self-test
//   tile_brightness_ref input.pgm output.pgm
//   tile_brightness_ref --compare a.pgm b.pgm

#include "../../fpga/hls/tile_brightness_core.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr int kPixels = kWidth * kHeight;

std::uint8_t naive_pixel(const std::uint8_t* image, int cx, int cy) {
    std::uint32_t win_sum = 0;
    for (int y = cy - 16; y <= cy + 15; ++y) {
        const std::uint8_t* row = image + static_cast<std::size_t>(y) * kWidth;
        for (int x = cx - 16; x <= cx + 15; ++x) {
            win_sum += row[x];
        }
    }
    return shade_pixel(image[static_cast<std::size_t>(cy) * kWidth + cx], win_sum);
}

bool expect(bool ok, const std::string& message) {
    if (!ok) {
        std::cerr << "FAIL " << message << "\n";
    }
    return ok;
}

bool self_test() {
    bool ok = true;
    std::vector<std::uint8_t> flat(kPixels, 100);
    std::vector<std::uint8_t> out(kPixels);
    apply_brightness(flat.data(), out.data());
    bool flat_ok = true;
    for (int i = 0; i < kPixels; ++i) {
        if (out[i] != 128) {
            flat_ok = false;
            break;
        }
    }
    const int flat_corner = out[0];
    ok = expect(flat_ok, "flat field 100 did not become 128") && ok;

    std::vector<std::uint8_t> image(kPixels);
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            image[static_cast<std::size_t>(y) * kWidth + x] =
                static_cast<std::uint8_t>((x * 3 + y * 5) & 255);
        }
    }
    image[static_cast<std::size_t>(100) * kWidth + 100] = 200;
    const std::uint8_t hand = naive_pixel(image.data(), 100, 100);

    apply_brightness(image.data(), out.data());
    ok = expect(out[100 * kWidth + 100] == hand, "sliding result differs from the direct window at (100,100)") && ok;

    int mismatches = 0;
    for (int y = 16; y <= kHeight - 16; ++y) {
        for (int x = 16; x <= kWidth - 16; ++x) {
            if (out[static_cast<std::size_t>(y) * kWidth + x] != naive_pixel(image.data(), x, y)) {
                ++mismatches;
                if (mismatches == 1) {
                    std::cerr << "first interior mismatch at " << x << "," << y << "\n";
                }
            }
        }
    }
    ok = expect(mismatches == 0, std::to_string(mismatches) + " interior pixels differ from the direct window") && ok;

    int border_bad = 0;
    for (int y = 16; y <= kHeight - 16; ++y) {
        const std::uint8_t* row = out.data() + static_cast<std::size_t>(y) * kWidth;
        for (int x = 0; x < 16; ++x) {
            border_bad += row[x] != row[16];
        }
        for (int x = kWidth - 15; x < kWidth; ++x) {
            border_bad += row[x] != row[kWidth - 16];
        }
    }
    for (int y = 0; y < 16; ++y) {
        border_bad += std::memcmp(out.data() + static_cast<std::size_t>(y) * kWidth,
                                   out.data() + static_cast<std::size_t>(16) * kWidth, kWidth) != 0;
    }
    for (int y = kHeight - 15; y < kHeight; ++y) {
        border_bad += std::memcmp(out.data() + static_cast<std::size_t>(y) * kWidth,
                                   out.data() + static_cast<std::size_t>(kHeight - 16) * kWidth, kWidth) != 0;
    }
    ok = expect(border_bad == 0, "outer 16 pixels are not copies of the nearest full window") && ok;

    if (ok) {
        std::cout << "self-test passed (" << kWidth << "x" << kHeight
                  << ", window " << kWindow << ", interior matches the direct sum)\n";
        std::cout << "output flat(0,0)=" << flat_corner
                  << " pattern(0,0)=" << static_cast<int>(out[0])
                  << " pattern(100,100)=" << static_cast<int>(out[100 * kWidth + 100])
                  << " pattern(1279,799)=" << static_cast<int>(out[kPixels - 1]) << "\n";
    }
    return ok;
}

bool read_image(const std::string& path, std::vector<std::uint8_t>& pixels) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "Cannot open " << path << "\n";
        return false;
    }
    char magic[2] = {};
    in.read(magic, 2);
    if (magic[0] == 'P' && magic[1] == '5') {
        in.clear();
        int width = -1;
        int height = -1;
        int maxv = -1;
        while (in && (width < 0 || height < 0 || maxv < 0)) {
            in >> std::ws;
            if (in.peek() == '#') {
                std::string skip;
                std::getline(in, skip);
                continue;
            }
            if (width < 0) {
                in >> width;
            } else if (height < 0) {
                in >> height;
            } else {
                in >> maxv;
            }
        }
        if (width != kWidth || height != kHeight || maxv != 255) {
            std::cerr << "PGM must be " << kWidth << "x" << kHeight << " max 255\n";
            return false;
        }
        in.get();
        pixels.resize(kPixels);
        in.read(reinterpret_cast<char*>(pixels.data()), kPixels);
        if (in.gcount() != kPixels) {
            std::cerr << "PGM is truncated\n";
            return false;
        }
        return true;
    }

    in.clear();
    in.seekg(0, std::ios::end);
    const std::streamoff bytes = in.tellg();
    if (bytes != kPixels) {
        std::cerr << path << " is neither P5 PGM nor raw " << kWidth << "x" << kHeight << "\n";
        return false;
    }
    in.seekg(0);
    pixels.resize(kPixels);
    in.read(reinterpret_cast<char*>(pixels.data()), kPixels);
    return in.gcount() == kPixels;
}

bool write_pgm(const std::string& path, const std::vector<std::uint8_t>& pixels) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::cerr << "Cannot write " << path << "\n";
        return false;
    }
    out << "P5\n" << kWidth << " " << kHeight << "\n255\n";
    out.write(reinterpret_cast<const char*>(pixels.data()), kPixels);
    return static_cast<bool>(out);
}

int compare_images(const std::string& left_path, const std::string& right_path) {
    std::vector<std::uint8_t> left;
    std::vector<std::uint8_t> right;
    if (!read_image(left_path, left) || !read_image(right_path, right)) {
        return 2;
    }
    int mismatches = 0;
    int max_abs = 0;
    long long sum_abs = 0;
    int first_x = 0;
    int first_y = 0;
    for (int i = 0; i < kPixels; ++i) {
        const int delta = std::abs(static_cast<int>(left[i]) - static_cast<int>(right[i]));
        if (delta != 0) {
            if (mismatches == 0) {
                first_x = i % kWidth;
                first_y = i / kWidth;
            }
            ++mismatches;
            sum_abs += delta;
            if (delta > max_abs) {
                max_abs = delta;
            }
        }
    }
    if (mismatches == 0) {
        std::cout << "match " << kPixels << " pixels\n";
        return 0;
    }
    std::cout << "differ " << mismatches << "/" << kPixels << " pixels, max abs " << max_abs << ", mean abs "
              << (static_cast<double>(sum_abs) / mismatches) << " on differing pixels, first " << first_x << ","
              << first_y << "\n";
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--self-test") {
        return self_test() ? 0 : 1;
    }
    if (argc == 4 && std::string(argv[1]) == "--compare") {
        return compare_images(argv[2], argv[3]);
    }
    if (argc == 3) {
        std::vector<std::uint8_t> input;
        if (!read_image(argv[1], input)) {
            return 2;
        }
        std::vector<std::uint8_t> output(kPixels);
        apply_brightness(input.data(), output.data());
        if (!write_pgm(argv[2], output)) {
            return 2;
        }
        std::cout << "wrote " << argv[2] << "\n";
        return 0;
    }

    std::cerr << "Usage:\n"
              << "  tile_brightness_ref --self-test\n"
              << "  tile_brightness_ref input.pgm output.pgm\n"
              << "  tile_brightness_ref --compare a.pgm b.pgm\n";
    return 2;
}
