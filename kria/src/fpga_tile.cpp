#include "fpga_tile.hpp"

#include "logging.hpp"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::uint32_t kControl = 0x00;
constexpr std::uint32_t kImageInLo = 0x10;
constexpr std::uint32_t kImageInHi = 0x14;
constexpr std::uint32_t kImageOutLo = 0x1c;
constexpr std::uint32_t kImageOutHi = 0x20;
constexpr std::uint32_t kWidth = 0x28;
constexpr std::uint32_t kHeight = 0x30;
constexpr std::uint32_t kTileW = 0x38;
constexpr std::uint32_t kTileH = 0x40;
constexpr std::uint32_t kCycles = 0x48;

constexpr std::size_t kBufferBytes = 2 * 1024 * 1024;

}  // namespace

FpgaTile::FpgaTile() = default;

FpgaTile::~FpgaTile() {
    if (regs_ != nullptr && reg_span_ != 0) {
        ::munmap(const_cast<std::uint32_t*>(regs_), reg_span_);
    }
    if (frame_map_ != nullptr && frame_bytes_ != 0) {
        ::munmap(frame_map_, frame_bytes_);
    }
    if (frame_fd_ >= 0) {
        ::close(frame_fd_);
    }
    if (uio_fd_ >= 0) {
        ::close(uio_fd_);
    }
}

bool FpgaTile::map_registers(std::string& error) {
    std::string uio_path;
    for (int index = 0; index < 16; ++index) {
        const std::string addr_path = "/sys/class/uio/uio" + std::to_string(index) + "/maps/map0/addr";
        std::ifstream addr_file(addr_path);
        if (!addr_file) {
            continue;
        }
        unsigned long long addr = 0;
        addr_file >> std::hex >> addr;
        if (addr == 0xA0000000ull) {
            uio_path = "/dev/uio" + std::to_string(index);
            const std::string size_path = "/sys/class/uio/uio" + std::to_string(index) + "/maps/map0/size";
            std::ifstream size_file(size_path);
            unsigned long long span = 0x10000;
            size_file >> std::hex >> span;
            uio_fd_ = ::open(uio_path.c_str(), O_RDWR | O_SYNC);
            if (uio_fd_ < 0) {
                error = "Cannot open " + uio_path;
                return false;
            }
            if (span < 0x100) {
                span = 0x10000;
            }
            void* mapped = ::mmap(nullptr, static_cast<std::size_t>(span), PROT_READ | PROT_WRITE, MAP_SHARED, uio_fd_, 0);
            if (mapped == MAP_FAILED) {
                error = "Failed to map " + uio_path;
                ::close(uio_fd_);
                uio_fd_ = -1;
                return false;
            }
            regs_ = static_cast<volatile std::uint32_t*>(mapped);
            reg_span_ = static_cast<std::size_t>(span);
            log_line("Using " + uio_path + " for the tile core");
            return true;
        }
    }

    const int mem = ::open("/dev/mem", O_RDWR | O_SYNC);
    if (mem < 0) {
        error = "FPGA tile core is not loaded (/dev/uio0 and /dev/mem are unavailable)";
        return false;
    }
    void* mapped = ::mmap(nullptr, 0x10000, PROT_READ | PROT_WRITE, MAP_SHARED, mem, 0xA0000000);
    ::close(mem);
    if (mapped == MAP_FAILED) {
        error = "Failed to map tile core at 0xA0000000";
        return false;
    }
    regs_ = static_cast<volatile std::uint32_t*>(mapped);
    reg_span_ = 0x10000;
    return true;
}

bool FpgaTile::map_cma(std::string& error) {
    frame_fd_ = ::open("/dev/fpga_frame", O_RDWR);
    if (frame_fd_ < 0) {
        error = "Cannot open /dev/fpga_frame";
        return false;
    }
    std::uint64_t base = 0;
    if (::ioctl(frame_fd_, 1, &base) != 0) {
        error = "Cannot read the low-DDR frame address";
        return false;
    }
    constexpr std::size_t kBytes = 4 * 1024 * 1024;
    void* mapped = ::mmap(nullptr, kBytes, PROT_READ | PROT_WRITE, MAP_SHARED, frame_fd_, 0);
    if (mapped == MAP_FAILED) {
        error = "Cannot map the low-DDR frame buffer";
        return false;
    }
    if (base + kBytes > 0x80000000ull) {
        ::munmap(mapped, kBytes);
        error = "Frame buffer is outside the low 2 GB visible to the FPGA";
        return false;
    }
    frame_map_ = mapped;
    frame_bytes_ = kBytes;
    input_.virt = mapped;
    input_.phys = base;
    input_.bytes = 2 * 1024 * 1024;
    output_.virt = static_cast<std::uint8_t*>(mapped) + input_.bytes;
    output_.phys = base + input_.bytes;
    output_.bytes = input_.bytes;
    char text[32];
    std::snprintf(text, sizeof(text), "%llx", static_cast<unsigned long long>(base));
    log_line(std::string("FPGA frame physical 0x") + text);
    return true;
}

bool FpgaTile::open(std::string& error) {
    if (!map_registers(error)) {
        return false;
    }
    if (!map_cma(error)) {
        return false;
    }
    log_line("FPGA tile core registers are mapped");
    return true;
}

void FpgaTile::write_reg(std::uint32_t offset, std::uint32_t value) {
    regs_[offset / 4] = value;
}

std::uint32_t FpgaTile::read_reg(std::uint32_t offset) const {
    return regs_[offset / 4];
}

bool FpgaTile::process(const cv::Mat& gray, cv::Mat& output, int tile_width, int tile_height,
                       std::uint32_t& cycles, double& wait_us, std::string& error) {
    if (regs_ == nullptr || gray.empty() || gray.type() != CV_8UC1) {
        error = "FPGA tile core is not ready";
        return false;
    }
    const std::size_t bytes = static_cast<std::size_t>(gray.cols) * static_cast<std::size_t>(gray.rows);
    if (bytes > kBufferBytes) {
        error = "Frame is larger than the FPGA buffer";
        return false;
    }

    auto* dst = static_cast<std::uint8_t*>(input_.virt);
    if (gray.isContinuous()) {
        std::memcpy(dst, gray.data, bytes);
    } else {
        for (int y = 0; y < gray.rows; ++y) {
            std::memcpy(dst + static_cast<std::size_t>(y) * gray.cols, gray.ptr(y), gray.cols);
        }
    }
    __builtin___clear_cache(reinterpret_cast<char*>(input_.virt),
                            reinterpret_cast<char*>(input_.virt) + bytes);

    write_reg(kImageInLo, static_cast<std::uint32_t>(input_.phys));
    write_reg(kImageInHi, static_cast<std::uint32_t>(input_.phys >> 32));
    write_reg(kImageOutLo, static_cast<std::uint32_t>(output_.phys));
    write_reg(kImageOutHi, static_cast<std::uint32_t>(output_.phys >> 32));
    write_reg(kWidth, static_cast<std::uint32_t>(gray.cols));
    write_reg(kHeight, static_cast<std::uint32_t>(gray.rows));
    write_reg(kTileW, static_cast<std::uint32_t>(tile_width));
    write_reg(kTileH, static_cast<std::uint32_t>(tile_height));

    const auto started = std::chrono::steady_clock::now();
    write_reg(kControl, 0x0);
    write_reg(kControl, 0x1);
    const auto deadline = started + std::chrono::seconds(2);
    while ((read_reg(kControl) & 0x2) == 0) {
        if (std::chrono::steady_clock::now() > deadline) {
            const std::uint32_t control = read_reg(kControl);
            error = "FPGA tile core timed out, control=0x" + std::to_string(control) + " phys_in=0x" +
                    std::to_string(input_.phys);
            return false;
        }
    }
    cycles = read_reg(kCycles);
    wait_us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count();

    __builtin___clear_cache(reinterpret_cast<char*>(output_.virt),
                            reinterpret_cast<char*>(output_.virt) + bytes);
    // The caller encodes this view before the next process() call reuses the buffer.
    output = cv::Mat(gray.rows, gray.cols, CV_8UC1, output_.virt);
    return true;
}
