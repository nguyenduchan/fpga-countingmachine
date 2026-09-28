// Laptop check for every Keyence row kernel. The HLS tops call these same functions.
//
// Build from this directory:
//   g++ -O2 -std=c++17 -Wno-unknown-pragmas keyence_ref.cpp -o keyence_ref

#include "../../fpga/hls/keyence/average.hpp"
#include "../../fpga/hls/keyence/binary.hpp"
#include "../../fpga/hls/keyence/blob.hpp"
#include "../../fpga/hls/keyence/blur.hpp"
#include "../../fpga/hls/keyence/contrast_conversion.hpp"
#include "../../fpga/hls/keyence/contrast_expansion.hpp"
#include "../../fpga/hls/keyence/expand.hpp"
#include "../../fpga/hls/keyence/image_extraction.hpp"
#include "../../fpga/hls/keyence/laplacian.hpp"
#include "../../fpga/hls/keyence/lumitrax.hpp"
#include "../../fpga/hls/keyence/median.hpp"
#include "../../fpga/hls/keyence/noise_isolation.hpp"
#include "../../fpga/hls/keyence/prewitt.hpp"
#include "../../fpga/hls/keyence/preserve_intensity.hpp"
#include "../../fpga/hls/keyence/remove_bright_noise.hpp"
#include "../../fpga/hls/keyence/remove_dark_noise.hpp"
#include "../../fpga/hls/keyence/roberts.hpp"
#include "../../fpga/hls/keyence/scratch_defect.hpp"
#include "../../fpga/hls/keyence/shading.hpp"
#include "../../fpga/hls/keyence/sharpen.hpp"
#include "../../fpga/hls/keyence/shrink.hpp"
#include "../../fpga/hls/keyence/sobel.hpp"
#include "../../fpga/hls/keyence/sobel_x.hpp"
#include "../../fpga/hls/keyence/sobel_y.hpp"
#include "../../fpga/hls/keyence/subtraction.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr int kW = 64;
constexpr int kH = 48;

bool expect(bool ok, const std::string& name) {
    std::cout << (ok ? "PASS " : "FAIL ") << name << "\n";
    return ok;
}

int at(int x, int y) {
    return y * kW + x;
}

void fill(std::vector<std::uint8_t>* image, std::uint8_t value) {
    image->assign(static_cast<std::size_t>(kW * kH), value);
}

int count_eq(const std::vector<std::uint8_t>& image, std::uint8_t value) {
    int count = 0;
    for (std::uint8_t pixel : image) {
        if (pixel == value) {
            ++count;
        }
    }
    return count;
}

void paint_rect(std::vector<std::uint8_t>* image, int x0, int y0, int x1, int y1, std::uint8_t value) {
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            (*image)[static_cast<std::size_t>(at(x, y))] = value;
        }
    }
}

bool run() {
    bool ok = true;
    std::vector<std::uint8_t> image(kW * kH, 0);
    std::vector<std::uint8_t> out(kW * kH, 0);
    std::vector<std::uint8_t> other(kW * kH, 0);
    std::vector<std::uint8_t> scratch(kW * kH, 0);
    std::vector<std::uint16_t> labels(kW * kH, 0);

    fill(&image, 40);
    image[static_cast<std::size_t>(at(3, 3))] = 200;
    image[static_cast<std::size_t>(at(4, 4))] = 10;
    apply_binary(image.data(), out.data(), kW, kH, 128, 255);
    ok = expect(out[static_cast<std::size_t>(at(3, 3))] == 255 &&
                    out[static_cast<std::size_t>(at(4, 4))] == 0 &&
                    count_eq(out, 255) == 1,
                "binary") && ok;

    apply_contrast(image.data(), out.data(), kW, kH, 10, 256);
    ok = expect(out[static_cast<std::size_t>(at(1, 1))] == 50, "contrast") && ok;

    fill(&image, 180);
    image[static_cast<std::size_t>(at(20, 20))] = 0;
    apply_expand(image.data(), out.data(), kW, kH);
    ok = expect(out[static_cast<std::size_t>(at(20, 20))] == 180, "expand") && ok;

    fill(&image, 20);
    image[static_cast<std::size_t>(at(20, 20))] = 255;
    apply_shrink(image.data(), out.data(), kW, kH);
    ok = expect(out[static_cast<std::size_t>(at(20, 20))] == 20, "shrink") && ok;

    fill(&image, 180);
    image[static_cast<std::size_t>(at(20, 20))] = 0;
    apply_remove_dark_noise(image.data(), out.data(), scratch.data(), kW, kH);
    ok = expect(out[static_cast<std::size_t>(at(20, 20))] == 180, "remove dark noise") && ok;

    fill(&image, 20);
    image[static_cast<std::size_t>(at(20, 20))] = 255;
    apply_remove_bright_noise(image.data(), out.data(), scratch.data(), kW, kH);
    ok = expect(out[static_cast<std::size_t>(at(20, 20))] == 20, "remove bright noise") && ok;

    apply_median(image.data(), out.data(), kW, kH);
    ok = expect(out[static_cast<std::size_t>(at(20, 20))] == 20, "median") && ok;

    fill(&image, 33);
    apply_average(image.data(), out.data(), kW, kH);
    ok = expect(out[static_cast<std::size_t>(at(8, 8))] == 33, "average") && ok;
    apply_blur(image.data(), out.data(), kW, kH, 0);
    ok = expect(out[static_cast<std::size_t>(at(8, 8))] == 33, "blur") && ok;
    apply_sharpen(image.data(), out.data(), kW, kH);
    ok = expect(out[static_cast<std::size_t>(at(8, 8))] == 33, "sharpen") && ok;

    std::vector<std::uint8_t> shade_in(64 * 64, 100);
    std::vector<std::uint8_t> shade_out(64 * 64, 0);
    apply_shading(shade_in.data(), shade_out.data(), 64, 64);
    ok = expect(shade_out[0] == 128 && shade_out[32 * 64 + 32] == 128, "shading") && ok;

    fill(&image, 50);
    fill(&other, 100);
    apply_preserve_intensity(image.data(), other.data(), out.data(), kW, kH);
    ok = expect(out[static_cast<std::size_t>(at(8, 8))] == 100, "preserve intensity") && ok;

    fill(&image, 40);
    image[static_cast<std::size_t>(at(10, 10))] = 200;
    apply_contrast_expansion(image.data(), out.data(), kW, kH, 256, 0);
    ok = expect(out[static_cast<std::size_t>(at(2, 2))] == 0 && out[static_cast<std::size_t>(at(10, 10))] == 255,
                "contrast expansion") && ok;

    fill(&image, 180);
    fill(&other, 100);
    apply_subtraction(image.data(), other.data(), out.data(), kW, kH);
    ok = expect(out[static_cast<std::size_t>(at(8, 8))] == 80, "subtraction") && ok;

    fill(&image, 40);
    apply_image_extraction(image.data(), out.data(), scratch.data(), kW, kH);
    ok = expect(out[static_cast<std::size_t>(at(8, 8))] == 128, "image extraction") && ok;

    fill(&image, 0);
    for (int y = 0; y < kH; ++y) {
        for (int x = 32; x < kW; ++x) {
            image[static_cast<std::size_t>(at(x, y))] = 255;
        }
    }
    apply_sobel_x(image.data(), out.data(), kW, kH);
    ok = expect(out[static_cast<std::size_t>(at(32, 20))] == 255 && out[static_cast<std::size_t>(at(8, 20))] == 0,
                "sobel x") && ok;
    apply_prewitt(image.data(), out.data(), kW, kH);
    ok = expect(out[static_cast<std::size_t>(at(32, 20))] == 255, "prewitt") && ok;
    apply_roberts(image.data(), out.data(), kW, kH);
    ok = expect(out[static_cast<std::size_t>(at(31, 20))] == 255 && out[static_cast<std::size_t>(at(4, 4))] == 0,
                "roberts") && ok;
    apply_sobel(image.data(), out.data(), kW, kH);
    ok = expect(out[static_cast<std::size_t>(at(32, 20))] == 255 && out[static_cast<std::size_t>(at(8, 20))] == 0,
                "sobel") && ok;

    fill(&image, 0);
    for (int y = 16; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            image[static_cast<std::size_t>(at(x, y))] = 255;
        }
    }
    apply_sobel_y(image.data(), out.data(), kW, kH);
    ok = expect(out[static_cast<std::size_t>(at(20, 16))] == 255 && out[static_cast<std::size_t>(at(20, 4))] == 0,
                "sobel y") && ok;

    fill(&image, 30);
    apply_laplacian(image.data(), out.data(), kW, kH);
    ok = expect(out[static_cast<std::size_t>(at(12, 12))] == 0, "laplacian") && ok;

    fill(&image, 0);
    for (int x = 16; x < 48; ++x) {
        image[static_cast<std::size_t>(at(x, 24))] = 255;
    }
    apply_scratch_defect(image.data(), out.data(), kW, kH);
    ok = expect(out[static_cast<std::size_t>(at(32, 24))] > 0 && out[static_cast<std::size_t>(at(8, 8))] == 0,
                "scratch defect") && ok;

    fill(&image, 0);
    paint_rect(&image, 8, 8, 18, 18, 255);
    apply_blob(image.data(), out.data(), labels.data(), kW, kH, 10, 0, 1);
    ok = expect(count_eq(out, 255) == 100, "blob") && ok;

    fill(&image, 0);
    for (int x = 10; x <= 20; ++x) {
        image[static_cast<std::size_t>(at(x, 10))] = 255;
        image[static_cast<std::size_t>(at(x, 20))] = 255;
    }
    for (int y = 10; y <= 20; ++y) {
        image[static_cast<std::size_t>(at(10, y))] = 255;
        image[static_cast<std::size_t>(at(20, y))] = 255;
    }
    apply_blob(image.data(), out.data(), labels.data(), kW, kH, 1, 0, 1 | 2);
    ok = expect(count_eq(out, 255) == 121, "blob fill holes") && ok;

    fill(&image, 0);
    for (int y = 0; y < kH; ++y) {
        image[static_cast<std::size_t>(at(0, y))] = 255;
    }
    apply_blob(image.data(), out.data(), labels.data(), kW, kH, 1, 0, 1 | 4);
    ok = expect(count_eq(out, 255) == 0, "blob border") && ok;

    fill(&image, 30);
    image[static_cast<std::size_t>(at(24, 24))] = 255;
    paint_rect(&image, 30, 30, 40, 40, 255);
    apply_noise_isolation(image.data(), out.data(), scratch.data(), labels.data(), kW, kH, 5, 1);
    ok = expect(out[static_cast<std::size_t>(at(24, 24))] == 255 && out[static_cast<std::size_t>(at(35, 35))] == 0,
                "noise extract") && ok;
    apply_noise_isolation(image.data(), out.data(), scratch.data(), labels.data(), kW, kH, 5, 0);
    ok = expect(out[static_cast<std::size_t>(at(24, 24))] == 30 && out[static_cast<std::size_t>(at(35, 35))] == 255,
                "noise remove") && ok;

    fill(&image, 80);
    fill(&other, 80);
    std::vector<std::uint8_t> dir2 = image;
    std::vector<std::uint8_t> dir3 = image;
    apply_lumitrax(image.data(), other.data(), dir2.data(), dir3.data(), out.data(), scratch.data(), kW, kH);
    ok = expect(out[static_cast<std::size_t>(at(6, 6))] == 80 && scratch[static_cast<std::size_t>(at(6, 6))] == 0,
                "lumitrax flat") && ok;
    image[static_cast<std::size_t>(at(6, 6))] = 200;
    apply_lumitrax(image.data(), other.data(), dir2.data(), dir3.data(), out.data(), scratch.data(), kW, kH);
    ok = expect(out[static_cast<std::size_t>(at(6, 6))] == 110 && scratch[static_cast<std::size_t>(at(6, 6))] == 120,
                "lumitrax shape") && ok;

    return ok;
}

}  // namespace

int main() {
    if (!run()) {
        std::cerr << "keyence self-test failed\n";
        return 1;
    }
    std::cout << "keyence self-test passed\n";
    return 0;
}
