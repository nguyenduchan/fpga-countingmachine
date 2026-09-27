#pragma once

#include "config.hpp"

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <string>

class UsbCamera {
public:
    explicit UsbCamera(const AppConfig& config);

    bool open(std::string& error);
    bool read(cv::Mat& frame, std::string& error);
    void close();

    int width() const { return width_; }
    int height() const { return height_; }
    double fps() const { return fps_; }
    const std::string& pixel_format() const { return pixel_format_; }

private:
    bool try_open(int fourcc, bool convert_rgb, cv::Mat& first_frame, std::string& error);

    const AppConfig& config_;
    cv::VideoCapture capture_;
    int width_ = 0;
    int height_ = 0;
    double fps_ = 0.0;
    std::string pixel_format_;
};
