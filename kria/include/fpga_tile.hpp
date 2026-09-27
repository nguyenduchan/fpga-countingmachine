#pragma once

#include <opencv2/core.hpp>

#include <cstdint>

// Talks to the tile_brightness HLS kernel at 0xA0000000 after the bitstream is loaded.
class FpgaTile {
public:
    FpgaTile();
    ~FpgaTile();

    bool open(std::string& error);
    bool ready() const { return regs_ != nullptr; }

    // Copies gray into the FPGA buffers, runs the kernel, copies the result back.
    // cycles is the value returned by the kernel (processed cycles at 100 MHz).
    bool process(const cv::Mat& gray, cv::Mat& output, int tile_width, int tile_height,
                 std::uint32_t& cycles, double& wait_us, std::string& error);

private:
    struct Buffer {
        void* virt = nullptr;
        std::uint64_t phys = 0;
        std::size_t bytes = 0;
    };

    bool map_registers(std::string& error);
    bool map_cma(std::string& error);
    void write_reg(std::uint32_t offset, std::uint32_t value);
    std::uint32_t read_reg(std::uint32_t offset) const;

    int uio_fd_ = -1;
    int frame_fd_ = -1;
    void* frame_map_ = nullptr;
    std::size_t frame_bytes_ = 0;
    volatile std::uint32_t* regs_ = nullptr;
    std::size_t reg_span_ = 0;
    Buffer input_;
    Buffer output_;
};
