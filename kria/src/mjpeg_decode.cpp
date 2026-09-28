#include "mjpeg_decode.hpp"

#include "logging.hpp"

#include <turbojpeg.h>
#include <jpeglib.h>
#include <setjmp.h>

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstring>
#include <fstream>
#include <string>

namespace {

int xioctl(int fd, unsigned long request, void* argument) {
    int result = 0;
    do {
        result = ::ioctl(fd, request, argument);
    } while (result < 0 && errno == EINTR);
    return result;
}

std::string video_name(int index) {
    std::ifstream name_file("/sys/class/video4linux/video" + std::to_string(index) + "/name");
    std::string name;
    std::getline(name_file, name);
    return name;
}

bool name_is_usb_camera(const std::string& name) {
    std::string lower = name;
    for (char& ch : lower) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return lower.find("uvc") != std::string::npos;
}

bool queue_has_jpeg(int fd, unsigned type, std::uint32_t& fourcc) {
    fourcc = 0;
    for (std::uint32_t index = 0; index < 32; ++index) {
        v4l2_fmtdesc desc{};
        desc.index = index;
        desc.type = type;
        if (xioctl(fd, VIDIOC_ENUM_FMT, &desc) < 0) {
            break;
        }
        if (desc.pixelformat == V4L2_PIX_FMT_MJPEG) {
            fourcc = V4L2_PIX_FMT_MJPEG;
            return true;
        }
        if (desc.pixelformat == V4L2_PIX_FMT_JPEG && fourcc == 0) {
            fourcc = V4L2_PIX_FMT_JPEG;
        }
    }
    return fourcc != 0;
}

bool copy_luma(const std::uint8_t* source, std::size_t source_size, std::uint32_t pixfmt, int width, int height,
               std::uint32_t stride, std::vector<std::uint8_t>& gray) {
    if (source == nullptr || width < 1 || height < 1 || stride < 1) {
        return false;
    }
    const std::size_t pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const std::size_t rows = static_cast<std::size_t>(height) * static_cast<std::size_t>(stride);
    if (source_size < rows) {
        return false;
    }
    gray.resize(pixels);
    if (pixfmt == V4L2_PIX_FMT_YUYV) {
        if (stride < static_cast<std::uint32_t>(width) * 2u) {
            return false;
        }
        for (int y = 0; y < height; ++y) {
            const std::uint8_t* row = source + static_cast<std::size_t>(y) * stride;
            std::uint8_t* destination = gray.data() + static_cast<std::size_t>(y) * width;
            for (int x = 0; x < width; ++x) {
                destination[x] = row[static_cast<std::size_t>(x) * 2u];
            }
        }
        return true;
    }
    if (stride < static_cast<std::uint32_t>(width)) {
        return false;
    }
    if (stride == static_cast<std::uint32_t>(width)) {
        std::memcpy(gray.data(), source, pixels);
        return true;
    }
    for (int y = 0; y < height; ++y) {
        std::memcpy(gray.data() + static_cast<std::size_t>(y) * width,
                    source + static_cast<std::size_t>(y) * stride, static_cast<std::size_t>(width));
    }
    return true;
}

}  // namespace

MjpegDecoder::MjpegDecoder(std::string camera_device) : camera_device_(std::move(camera_device)) {}

MjpegDecoder::~MjpegDecoder() {
    close_hardware();
    if (turbo_ != nullptr) {
        tjDestroy(static_cast<tjhandle>(turbo_));
        turbo_ = nullptr;
    }
}

void MjpegDecoder::close_hardware() {
    if (fd_ >= 0) {
        int output_type = mplane_ ? V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE : V4L2_BUF_TYPE_VIDEO_OUTPUT;
        int capture_type = mplane_ ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
        xioctl(fd_, VIDIOC_STREAMOFF, &output_type);
        xioctl(fd_, VIDIOC_STREAMOFF, &capture_type);
    }
    streaming_ = false;
    if (output_map_ != nullptr && output_map_ != MAP_FAILED && output_length_ != 0) {
        ::munmap(output_map_, output_length_);
    }
    if (capture_map_ != nullptr && capture_map_ != MAP_FAILED && capture_length_ != 0) {
        ::munmap(capture_map_, capture_length_);
    }
    output_map_ = nullptr;
    capture_map_ = nullptr;
    output_length_ = 0;
    capture_length_ = 0;
    if (fd_ >= 0) {
        unsigned output_type =
            static_cast<unsigned>(mplane_ ? V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE : V4L2_BUF_TYPE_VIDEO_OUTPUT);
        unsigned capture_type =
            static_cast<unsigned>(mplane_ ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE);
        v4l2_requestbuffers request{};
        request.memory = V4L2_MEMORY_MMAP;
        request.count = 0;
        request.type = output_type;
        xioctl(fd_, VIDIOC_REQBUFS, &request);
        request.type = capture_type;
        xioctl(fd_, VIDIOC_REQBUFS, &request);
        ::close(fd_);
        fd_ = -1;
    }
    hardware_ = false;
    capture_fourcc_ = 0;
    capture_stride_ = 0;
}

bool MjpegDecoder::map_queue(int fd, unsigned type, bool mplane, bool output) {
    v4l2_requestbuffers request{};
    request.count = 1;
    request.type = type;
    request.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &request) < 0 || request.count < 1) {
        return false;
    }

    v4l2_plane plane{};
    v4l2_buffer buffer{};
    buffer.type = type;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.index = 0;
    if (mplane) {
        buffer.length = 1;
        buffer.m.planes = &plane;
    }
    if (xioctl(fd, VIDIOC_QUERYBUF, &buffer) < 0) {
        return false;
    }

    const std::size_t length = mplane ? plane.length : buffer.length;
    const off_t offset = mplane ? static_cast<off_t>(plane.m.mem_offset) : static_cast<off_t>(buffer.m.offset);
    void* mapped = ::mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);
    if (mapped == MAP_FAILED || length == 0) {
        return false;
    }
    if (output) {
        output_map_ = mapped;
        output_length_ = length;
    } else {
        capture_map_ = mapped;
        capture_length_ = length;
    }
    return true;
}

bool MjpegDecoder::configure(int fd, int width, int height, bool mplane) {
    const unsigned output_type = mplane ? V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE : V4L2_BUF_TYPE_VIDEO_OUTPUT;
    const unsigned capture_type = mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
    std::uint32_t jpeg_fourcc = 0;
    if (!queue_has_jpeg(fd, output_type, jpeg_fourcc)) {
        return false;
    }

    auto set_plane = [&](unsigned type, std::uint32_t fourcc, std::uint32_t image_bytes, std::uint32_t stride) {
        v4l2_format format{};
        format.type = type;
        if (mplane) {
            format.fmt.pix_mp.width = static_cast<std::uint32_t>(width);
            format.fmt.pix_mp.height = static_cast<std::uint32_t>(height);
            format.fmt.pix_mp.pixelformat = fourcc;
            format.fmt.pix_mp.field = V4L2_FIELD_NONE;
            format.fmt.pix_mp.num_planes = 1;
            format.fmt.pix_mp.plane_fmt[0].sizeimage = image_bytes;
            format.fmt.pix_mp.plane_fmt[0].bytesperline = stride;
        } else {
            format.fmt.pix.width = static_cast<std::uint32_t>(width);
            format.fmt.pix.height = static_cast<std::uint32_t>(height);
            format.fmt.pix.pixelformat = fourcc;
            format.fmt.pix.field = V4L2_FIELD_NONE;
            format.fmt.pix.sizeimage = image_bytes;
            format.fmt.pix.bytesperline = stride;
        }
        if (xioctl(fd, VIDIOC_S_FMT, &format) < 0) {
            return false;
        }
        const std::uint32_t got = mplane ? format.fmt.pix_mp.pixelformat : format.fmt.pix.pixelformat;
        const int got_width = static_cast<int>(mplane ? format.fmt.pix_mp.width : format.fmt.pix.width);
        const int got_height = static_cast<int>(mplane ? format.fmt.pix_mp.height : format.fmt.pix.height);
        if (got != fourcc || got_width != width || got_height != height) {
            return false;
        }
        if (type == capture_type) {
            capture_stride_ = mplane ? format.fmt.pix_mp.plane_fmt[0].bytesperline : format.fmt.pix.bytesperline;
            if (capture_stride_ == 0) {
                capture_stride_ = (fourcc == V4L2_PIX_FMT_YUYV) ? static_cast<std::uint32_t>(width) * 2u
                                                                : static_cast<std::uint32_t>(width);
            }
        }
        return true;
    };

    mplane_ = mplane;
    output_fourcc_ = jpeg_fourcc;
    constexpr std::uint32_t kJpegBytes = 2u * 1024u * 1024u;
    if (!set_plane(output_type, jpeg_fourcc, kJpegBytes, 0)) {
        return false;
    }

    const std::uint32_t gray_fourccs[] = {V4L2_PIX_FMT_GREY, V4L2_PIX_FMT_NV12, V4L2_PIX_FMT_YUYV};
    bool capture_ok = false;
    for (const std::uint32_t fourcc : gray_fourccs) {
        const std::uint32_t stride = (fourcc == V4L2_PIX_FMT_YUYV) ? static_cast<std::uint32_t>(width) * 2u
                                                                   : static_cast<std::uint32_t>(width);
        const std::uint32_t bytes = (fourcc == V4L2_PIX_FMT_NV12) ? stride * static_cast<std::uint32_t>(height) * 3u / 2u
                                                                  : stride * static_cast<std::uint32_t>(height);
        if (set_plane(capture_type, fourcc, bytes, stride)) {
            capture_fourcc_ = fourcc;
            capture_ok = true;
            break;
        }
    }
    if (!capture_ok) {
        return false;
    }
    if (!map_queue(fd, output_type, mplane, true) || !map_queue(fd, capture_type, mplane, false)) {
        return false;
    }

    int output = static_cast<int>(output_type);
    int capture = static_cast<int>(capture_type);
    if (xioctl(fd, VIDIOC_STREAMON, &output) < 0 || xioctl(fd, VIDIOC_STREAMON, &capture) < 0) {
        return false;
    }
    streaming_ = true;
    return true;
}

bool MjpegDecoder::open_hardware(int width, int height) {
    close_hardware();
    if (width < 1 || height < 1) {
        return false;
    }
    for (int index = 0; index < 64; ++index) {
        const std::string path = "/dev/video" + std::to_string(index);
        if (path == camera_device_ || name_is_usb_camera(video_name(index))) {
            continue;
        }
        const int fd = ::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }
        v4l2_capability caps{};
        if (xioctl(fd, VIDIOC_QUERYCAP, &caps) < 0) {
            ::close(fd);
            continue;
        }
        const std::uint32_t device_caps =
            (caps.capabilities & V4L2_CAP_DEVICE_CAPS) != 0 ? caps.device_caps : caps.capabilities;
        const bool mplane = (device_caps & V4L2_CAP_VIDEO_M2M_MPLANE) != 0;
        const bool single = (device_caps & V4L2_CAP_VIDEO_M2M) != 0;
        if (!mplane && !single) {
            ::close(fd);
            continue;
        }
        fd_ = fd;
        bool configured = false;
        if (mplane) {
            configured = configure(fd_, width, height, true);
            if (!configured && single) {
                close_hardware();
                fd_ = ::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
                if (fd_ < 0) {
                    continue;
                }
            }
        }
        if (!configured && single) {
            configured = configure(fd_, width, height, false);
        }
        if (!configured) {
            close_hardware();
            continue;
        }
        width_ = width;
        height_ = height;
        hardware_ = true;
        backend_ = "v4l2:" + path;
        log_line("JPEG decode using " + backend_ + " (" + video_name(index) + ")");
        return true;
    }
    backend_ = "turbojpeg, no hardware JPEG decoder";
    return false;
}

void MjpegDecoder::prepare(int width, int height) {
    if (turbo_ == nullptr) {
        turbo_ = tjInitDecompress();
    }
    if (!open_hardware(width, height)) {
        log_line("JPEG decode using turbojpeg. Kria K26 has no VCU, and no V4L2 JPEG decoder is loaded.");
    }
}

MjpegDecoder::HwResult MjpegDecoder::decode_hardware(const std::uint8_t* jpeg, std::size_t jpeg_size, int width,
                                                    int height, std::vector<std::uint8_t>& gray) {
    if (!hardware_ || fd_ < 0 || jpeg == nullptr || jpeg_size == 0 || width != width_ || height != height_) {
        return HwResult::Broken;
    }
    if (jpeg_size > output_length_) {
        return HwResult::Broken;
    }

    const unsigned output_type = mplane_ ? V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE : V4L2_BUF_TYPE_VIDEO_OUTPUT;
    const unsigned capture_type = mplane_ ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
    std::memcpy(output_map_, jpeg, jpeg_size);

    auto queue = [&](unsigned type, std::uint32_t bytes_used) {
        v4l2_plane plane{};
        v4l2_buffer buffer{};
        buffer.type = type;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = 0;
        if (mplane_) {
            buffer.length = 1;
            buffer.m.planes = &plane;
            plane.bytesused = bytes_used;
        } else {
            buffer.bytesused = bytes_used;
        }
        return xioctl(fd_, VIDIOC_QBUF, &buffer) == 0;
    };
    // 1 = dequeued, 0 = not ready yet, -1 = device error.
    auto dequeue = [&](unsigned type, std::uint32_t& bytes_used, std::uint32_t& data_offset, std::uint32_t& flags) {
        v4l2_plane plane{};
        v4l2_buffer buffer{};
        buffer.type = type;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = 0;
        if (mplane_) {
            buffer.length = 1;
            buffer.m.planes = &plane;
        }
        if (xioctl(fd_, VIDIOC_DQBUF, &buffer) < 0) {
            return errno == EAGAIN ? 0 : -1;
        }
        flags = buffer.flags;
        if (mplane_) {
            bytes_used = plane.bytesused;
            data_offset = plane.data_offset;
        } else {
            bytes_used = buffer.bytesused;
            data_offset = 0;
        }
        return 1;
    };

    if (!queue(capture_type, 0) || !queue(output_type, static_cast<std::uint32_t>(jpeg_size))) {
        return HwResult::Broken;
    }

    bool have_output = false;
    bool have_capture = false;
    std::uint32_t output_bytes = 0;
    std::uint32_t output_offset = 0;
    std::uint32_t output_flags = 0;
    std::uint32_t capture_bytes = 0;
    std::uint32_t capture_offset = 0;
    std::uint32_t capture_flags = 0;
    for (int attempt = 0; attempt < 4 && (!have_output || !have_capture); ++attempt) {
        pollfd wait_fd{};
        wait_fd.fd = fd_;
        wait_fd.events = POLLIN | POLLOUT | POLLPRI;
        const int waited = ::poll(&wait_fd, 1, 25);
        if (waited < 0 && errno == EINTR) {
            continue;
        }
        if (waited <= 0) {
            return HwResult::Broken;
        }
        if (!have_output) {
            const int got = dequeue(output_type, output_bytes, output_offset, output_flags);
            if (got < 0) {
                return HwResult::Broken;
            }
            have_output = got > 0;
        }
        if (!have_capture) {
            const int got = dequeue(capture_type, capture_bytes, capture_offset, capture_flags);
            if (got < 0) {
                return HwResult::Broken;
            }
            have_capture = got > 0;
        }
    }
    if (!have_output || !have_capture) {
        return HwResult::Broken;
    }
    (void)output_bytes;
    (void)output_offset;
    if ((capture_flags & V4L2_BUF_FLAG_ERROR) != 0 || (output_flags & V4L2_BUF_FLAG_ERROR) != 0) {
        return HwResult::BadFrame;
    }
    if (capture_offset >= capture_length_ || capture_bytes == 0) {
        return HwResult::BadFrame;
    }
    const std::size_t available = capture_length_ - capture_offset;
    const std::uint8_t* pixels = static_cast<const std::uint8_t*>(capture_map_) + capture_offset;
    const std::size_t used = capture_bytes < available ? capture_bytes : available;
    if (!copy_luma(pixels, used, capture_fourcc_, width, height, capture_stride_, gray)) {
        return HwResult::BadFrame;
    }
    return HwResult::Ok;
}

bool MjpegDecoder::decode_turbo(const std::uint8_t* jpeg, std::size_t jpeg_size, int width, int height,
                                std::vector<std::uint8_t>& gray) {
    if (turbo_ == nullptr || jpeg == nullptr || width < 1 || height < 1) {
        return false;
    }
    gray.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    return tjDecompress2(static_cast<tjhandle>(turbo_), jpeg, static_cast<unsigned long>(jpeg_size), gray.data(),
                         width, 0, height, TJPF_GRAY, TJFLAG_FASTDCT | TJFLAG_FASTUPSAMPLE) == 0;
}

namespace {

struct JpegJump {
    jpeg_error_mgr pub;
    jmp_buf jump;
};

void jpeg_fail(j_common_ptr cinfo) {
    longjmp(reinterpret_cast<JpegJump*>(cinfo->err)->jump, 1);
}

}  // namespace

bool MjpegDecoder::decode_rows(const std::uint8_t* jpeg, std::size_t jpeg_size, int width, int height,
                               std::uint8_t* destination, void (*sink)(void*, int), void* ctx) {
    if (jpeg == nullptr || destination == nullptr || width < 1 || height < 1 || jpeg_size == 0) {
        return false;
    }
    jpeg_decompress_struct info{};
    JpegJump error{};
    info.err = jpeg_std_error(&error.pub);
    error.pub.error_exit = jpeg_fail;
    if (setjmp(error.jump) != 0) {
        jpeg_destroy_decompress(&info);
        return false;
    }
    jpeg_create_decompress(&info);
    jpeg_mem_src(&info, jpeg, static_cast<unsigned long>(jpeg_size));
    if (jpeg_read_header(&info, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&info);
        return false;
    }
    info.out_color_space = JCS_GRAYSCALE;
    if (!jpeg_start_decompress(&info) || info.output_width != static_cast<unsigned>(width) ||
        info.output_height != static_cast<unsigned>(height) || info.output_components != 1) {
        jpeg_destroy_decompress(&info);
        return false;
    }
    while (info.output_scanline < info.output_height) {
        const int row = static_cast<int>(info.output_scanline);
        std::uint8_t* line = destination + static_cast<std::size_t>(row) * static_cast<std::size_t>(width);
        if (jpeg_read_scanlines(&info, &line, 1) != 1) {
            jpeg_destroy_decompress(&info);
            return false;
        }
        if (sink != nullptr) {
            sink(ctx, row + 1);
        }
    }
    jpeg_finish_decompress(&info);
    jpeg_destroy_decompress(&info);
    return true;
}

bool MjpegDecoder::decode(const std::uint8_t* jpeg, std::size_t jpeg_size, int width, int height,
                          std::vector<std::uint8_t>& gray) {
    if (hardware_ && (width != width_ || height != height_)) {
        open_hardware(width, height);
    }
    if (hardware_ && jpeg_size > output_length_) {
        return decode_turbo(jpeg, jpeg_size, width, height, gray);
    }
    if (hardware_) {
        const HwResult result = decode_hardware(jpeg, jpeg_size, width, height, gray);
        if (result == HwResult::Ok) {
            return true;
        }
        if (result == HwResult::BadFrame) {
            return false;
        }
        log_line("V4L2 JPEG decoder failed, continuing with turbojpeg");
        close_hardware();
        backend_ = "turbojpeg, hardware JPEG decode failed";
    }
    return decode_turbo(jpeg, jpeg_size, width, height, gray);
}
