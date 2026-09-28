#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Turns one OV9281 MJPEG frame into 8-bit luminance.
// The preferred path is a V4L2 memory-to-memory JPEG decoder, which is the
// device behind GStreamer's v4l2jpegdec. Decode starts as soon as a USB
// buffer is queued, so it overlaps the next camera read.
//
// The Kria K26 has no VCU. omxh264dec decodes H.264, not JPEG. When no JPEG
// M2M device is present, frames fall back to turbojpeg.
class MjpegDecoder {
public:
    explicit MjpegDecoder(std::string camera_device);
    ~MjpegDecoder();

    MjpegDecoder(const MjpegDecoder&) = delete;
    MjpegDecoder& operator=(const MjpegDecoder&) = delete;

    void prepare(int width, int height);
    bool decode(const std::uint8_t* jpeg, std::size_t jpeg_size, int width, int height,
                std::vector<std::uint8_t>& gray);
    // Writes grayscale scanlines straight into destination. After each row, sink(ctx, rows_ready)
    // is called so the FPGA can read that row from the shared buffer.
    bool decode_rows(const std::uint8_t* jpeg, std::size_t jpeg_size, int width, int height,
                     std::uint8_t* destination, void (*sink)(void* ctx, int rows_ready), void* ctx);
    const std::string& backend() const { return backend_; }

private:
    void close_hardware();
    bool open_hardware(int width, int height);
    bool configure(int fd, int width, int height, bool mplane);
    bool map_queue(int fd, unsigned type, bool mplane, bool output);
    // Broken means the device can no longer be used. BadFrame drops this JPEG only.
    enum class HwResult { Ok, BadFrame, Broken };
    HwResult decode_hardware(const std::uint8_t* jpeg, std::size_t jpeg_size, int width, int height,
                             std::vector<std::uint8_t>& gray);
    bool decode_turbo(const std::uint8_t* jpeg, std::size_t jpeg_size, int width, int height,
                      std::vector<std::uint8_t>& gray);

    std::string camera_device_;
    std::string backend_ = "turbojpeg";
    int fd_ = -1;
    bool mplane_ = false;
    bool streaming_ = false;
    bool hardware_ = false;
    std::uint32_t output_fourcc_ = 0;
    std::uint32_t capture_fourcc_ = 0;
    std::uint32_t capture_stride_ = 0;
    int width_ = 0;
    int height_ = 0;
    void* output_map_ = nullptr;
    std::size_t output_length_ = 0;
    void* capture_map_ = nullptr;
    std::size_t capture_length_ = 0;
    void* turbo_ = nullptr;
};
