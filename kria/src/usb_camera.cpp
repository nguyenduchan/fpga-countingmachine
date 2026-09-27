#include "usb_camera.hpp"

#include "logging.hpp"

#include <opencv2/videoio.hpp>

namespace {

std::string fourcc_text(int fourcc) {
    if (fourcc == 0) {
        return "default";
    }
    char text[5] = {
        static_cast<char>(fourcc & 0xff),
        static_cast<char>((fourcc >> 8) & 0xff),
        static_cast<char>((fourcc >> 16) & 0xff),
        static_cast<char>((fourcc >> 24) & 0xff),
        '\0'};
    for (char& ch : text) {
        if (ch != '\0' && (ch < 32 || ch > 126)) {
            ch = '?';
        }
    }
    return text;
}

}  // namespace

UsbCamera::UsbCamera(const AppConfig& config) : config_(config) {}

bool UsbCamera::try_open(int fourcc, bool convert_rgb, cv::Mat& first_frame, std::string& error) {
    capture_.release();
    capture_.open(config_.video_device, cv::CAP_V4L2);
    if (!capture_.isOpened()) {
        error = "Cannot open " + config_.video_device;
        return false;
    }

    if (fourcc != 0) {
        capture_.set(cv::CAP_PROP_FOURCC, fourcc);
    }
    capture_.set(cv::CAP_PROP_FRAME_WIDTH, config_.width);
    capture_.set(cv::CAP_PROP_FRAME_HEIGHT, config_.height);
    capture_.set(cv::CAP_PROP_FPS, config_.fps);
    capture_.set(cv::CAP_PROP_CONVERT_RGB, convert_rgb ? 1.0 : 0.0);
    // One V4L2 buffer makes this UVC camera drop to about half rate.
    // Several buffers let it run at its maximum frame rate.
    capture_.set(cv::CAP_PROP_BUFFERSIZE, 8);

    first_frame.release();
    for (int attempt = 0; attempt < 8; ++attempt) {
        cv::Mat frame;
        try {
            if (capture_.read(frame) && !frame.empty()) {
                first_frame = frame;
                return true;
            }
        } catch (const cv::Exception& ex) {
            error = ex.what();
            capture_.release();
            return false;
        }
    }

    error = "No frame from " + config_.video_device + " format " + fourcc_text(fourcc);
    capture_.release();
    return false;
}

bool UsbCamera::open(std::string& error) {
    close();
    capture_.open(config_.video_device, cv::CAP_V4L2);
    if (!capture_.isOpened()) {
        error = "Cannot open " + config_.video_device + " (" + config_.camera_model + ")";
        return false;
    }
    capture_.release();

    // MJPG is first: this OV9281 module does 1280x800 MJPEG at 30/60/120 fps.
    // YUYV at that size is limited to 10 fps and then has to be encoded again.
    const int formats[] = {
        cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
        cv::VideoWriter::fourcc('G', 'R', 'E', 'Y'),
        cv::VideoWriter::fourcc('Y', '8', '0', '0'),
        cv::VideoWriter::fourcc('Y', 'U', 'Y', 'V'),
        0};

    cv::Mat first_frame;
    std::string last_error;
    for (const bool convert_rgb : {false, true}) {
        for (const int fourcc : formats) {
            if (try_open(fourcc, convert_rgb, first_frame, last_error)) {
                width_ = first_frame.cols;
                height_ = first_frame.rows;
                fps_ = capture_.get(cv::CAP_PROP_FPS);
                const int actual = static_cast<int>(capture_.get(cv::CAP_PROP_FOURCC));
                pixel_format_ = fourcc_text(actual);
                log_line("Opened " + config_.camera_model + " on " + config_.video_device + " " +
                         std::to_string(width_) + "x" + std::to_string(height_) + " " + pixel_format_ +
                         " convert_rgb=" + (convert_rgb ? "1" : "0"));
                return true;
            }
            error = last_error;
        }
    }
    return false;
}

bool UsbCamera::read(cv::Mat& frame, std::string& error) {
    if (!capture_.isOpened()) {
        error = "Camera is not open";
        return false;
    }
    try {
        if (!capture_.read(frame) || frame.empty()) {
            error = "Camera read failed on " + config_.video_device;
            return false;
        }
        return true;
    } catch (const cv::Exception& ex) {
        error = ex.what();
        return false;
    }
}

void UsbCamera::close() {
    if (capture_.isOpened()) {
        capture_.release();
    }
    width_ = 0;
    height_ = 0;
    fps_ = 0.0;
    pixel_format_.clear();
}
