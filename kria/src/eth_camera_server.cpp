#include "eth_camera_server.hpp"

#include "fpga_tile.hpp"
#include "logging.hpp"
#include "protocol.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <turbojpeg.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

volatile std::sig_atomic_t g_stop = 0;
int g_listen_fd = -1;
int g_client_fd = -1;

void handle_signal(int) {
    g_stop = 1;
    if (g_listen_fd >= 0) {
        ::shutdown(g_listen_fd, SHUT_RDWR);
    }
    if (g_client_fd >= 0) {
        ::shutdown(g_client_fd, SHUT_RDWR);
    }
}

bool send_all(int fd, const uint8_t* data, std::size_t size) {
    std::size_t sent = 0;
    while (sent < size) {
        const ssize_t wrote = ::send(fd, data + sent, size - sent, MSG_NOSIGNAL);
        if (wrote < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (wrote == 0) {
            return false;
        }
        sent += static_cast<std::size_t>(wrote);
    }
    return true;
}

bool recv_all(int fd, uint8_t* data, std::size_t size) {
    std::size_t got = 0;
    while (got < size) {
        const ssize_t read = ::recv(fd, data + got, size - got, 0);
        if (read < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (read == 0) {
            return false;
        }
        got += static_cast<std::size_t>(read);
    }
    return true;
}

bool send_message(int fd, uint16_t type, const std::vector<uint8_t>& payload) {
    const std::vector<uint8_t> message = ethcam::encode_message(type, payload);
    return send_all(fd, message.data(), message.size());
}

bool send_error(int fd, const std::string& message) {
    const std::vector<uint8_t> payload(message.begin(), message.end());
    return send_message(fd, ethcam::kError, payload);
}

std::vector<uint8_t> hello_payload(const AppConfig& config) {
    std::vector<uint8_t> payload;
    ethcam::append_str(payload, config.board_name);
    ethcam::append_str(payload, config.ubuntu_version);
    ethcam::append_str(payload, config.camera_model);
    ethcam::append_u32(payload, static_cast<uint32_t>(config.width));
    ethcam::append_u32(payload, static_cast<uint32_t>(config.height));
    ethcam::append_u32(payload, static_cast<uint32_t>(config.fps));
    return payload;
}

enum class CommandType { Start, Stop };

struct Command {
    CommandType type;
    uint32_t session_id;
};

struct SessionControl {
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<Command> commands;
    std::atomic<bool> alive{true};
};

// false means the socket closed. An empty optional means the packet was ignored.
// This thread only reads. The session thread is the only writer on the socket.
bool read_command(int fd, std::optional<Command>& command) {
    command.reset();
    uint8_t header[ethcam::kHeaderSize];
    if (!recv_all(fd, header, sizeof(header))) {
        return false;
    }
    if (!ethcam::magic_ok(header)) {
        log_line("Rejected packet with a bad magic header");
        return false;
    }
    const uint16_t version = ethcam::load_u16(header + 4);
    const uint16_t type = ethcam::load_u16(header + 6);
    const uint32_t payload_len = ethcam::load_u32(header + 8);
    if (version != ethcam::kVersion || payload_len > ethcam::kMaxPayload) {
        log_line("Rejected packet version or payload length");
        return false;
    }
    std::vector<uint8_t> payload(payload_len);
    if (payload_len > 0 && !recv_all(fd, payload.data(), payload.size())) {
        return false;
    }
    if ((type == ethcam::kStart || type == ethcam::kStop) && payload.size() >= 4) {
        command = Command{
            type == ethcam::kStart ? CommandType::Start : CommandType::Stop,
            ethcam::load_u32(payload.data())};
        return true;
    }
    log_line("Ignored client message type " + std::to_string(type));
    return true;
}

void reader_loop(int fd, SessionControl& control) {
    while (control.alive.load()) {
        std::optional<Command> command;
        if (!read_command(fd, command)) {
            control.alive = false;
            control.cv.notify_all();
            return;
        }
        if (!command.has_value()) {
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(control.mutex);
            control.commands.push_back(*command);
        }
        control.cv.notify_one();
    }
}

bool take_next_command(SessionControl& control, Command& command) {
    std::unique_lock<std::mutex> lock(control.mutex);
    control.cv.wait(lock, [&] { return !control.alive.load() || !control.commands.empty(); });
    if (control.commands.empty()) {
        return false;
    }
    command = control.commands.front();
    control.commands.pop_front();
    return true;
}

// Returns true when the current session should end.
// A newer START stays in the queue so the outer loop can begin it.
bool session_should_end(SessionControl& control, uint32_t session_id) {
    std::lock_guard<std::mutex> lock(control.mutex);
    while (!control.commands.empty()) {
        const Command& command = control.commands.front();
        if (command.type == CommandType::Start) {
            return true;
        }
        control.commands.pop_front();
        if (command.type == CommandType::Stop &&
            (command.session_id == session_id || command.session_id == 0)) {
            return true;
        }
    }
    return !control.alive.load();
}

std::vector<uint8_t> encode_jpeg(const cv::Mat& frame, int quality, std::string& error) {
    cv::Mat gray;
    if (frame.channels() == 1) {
        gray = frame;
    } else if (frame.channels() == 3) {
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    } else if (frame.channels() == 4) {
        cv::cvtColor(frame, gray, cv::COLOR_BGRA2GRAY);
    } else if (frame.channels() == 2) {
        cv::cvtColor(frame, gray, cv::COLOR_YUV2GRAY_YUY2);
    } else {
        gray = frame;
    }

    try {
        std::vector<uint8_t> jpeg;
        const std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, quality};
        if (!cv::imencode(".jpg", gray, jpeg, params) || jpeg.empty()) {
            error = "JPEG encode failed";
            return {};
        }
        return jpeg;
    } catch (const cv::Exception& ex) {
        error = ex.what();
        return {};
    }
}

bool jpeg_dimensions(const uint8_t* data, std::size_t size, int& width, int& height) {
    if (size < 4 || data[0] != 0xFF || data[1] != 0xD8) {
        return false;
    }
    std::size_t index = 2;
    while (index + 4 < size) {
        if (data[index] != 0xFF) {
            ++index;
            continue;
        }
        while (index < size && data[index] == 0xFF) {
            ++index;
        }
        if (index >= size) {
            break;
        }
        const uint8_t marker = data[index++];
        if (marker == 0xD8 || marker == 0x01) {
            continue;
        }
        if (marker == 0xD9 || marker == 0xDA) {
            break;
        }
        if (index + 1 >= size) {
            break;
        }
        const uint16_t segment = static_cast<uint16_t>((static_cast<uint16_t>(data[index]) << 8) | data[index + 1]);
        if (segment < 2 || index + segment > size) {
            break;
        }
        const bool sof = (marker >= 0xC0 && marker <= 0xC3) || (marker >= 0xC5 && marker <= 0xC7) ||
                         (marker >= 0xC9 && marker <= 0xCB) || (marker >= 0xCD && marker <= 0xCF);
        if (sof && segment >= 7) {
            height = (data[index + 3] << 8) | data[index + 4];
            width = (data[index + 5] << 8) | data[index + 6];
            return width > 0 && height > 0;
        }
        index += segment;
    }
    return false;
}

bool take_camera_jpeg(const cv::Mat& frame, std::vector<uint8_t>& jpeg, int& width, int& height) {
    if (frame.empty() || !frame.isContinuous()) {
        return false;
    }
    const auto* data = frame.ptr<uint8_t>();
    const std::size_t size = frame.total() * frame.elemSize();
    if (size < 4 || data[0] != 0xFF || data[1] != 0xD8 || data[2] != 0xFF) {
        return false;
    }
    if (!jpeg_dimensions(data, size, width, height)) {
        return false;
    }
    jpeg.assign(data, data + size);
    return true;
}

std::vector<uint8_t> frame_payload(uint32_t session_id, uint32_t index, uint64_t timestamp_us, int width,
                                   int height, const std::vector<uint8_t>& image, uint32_t format) {
    std::vector<uint8_t> payload;
    payload.reserve(32 + image.size());
    ethcam::append_u32(payload, session_id);
    ethcam::append_u32(payload, index);
    ethcam::append_u64(payload, timestamp_us);
    ethcam::append_u32(payload, static_cast<uint32_t>(width));
    ethcam::append_u32(payload, static_cast<uint32_t>(height));
    ethcam::append_u32(payload, format);
    ethcam::append_u32(payload, static_cast<uint32_t>(image.size()));
    payload.insert(payload.end(), image.begin(), image.end());
    return payload;
}

std::vector<uint8_t> encode_strips(const uint8_t* gray, int width, int height, int quality) {
    const int bands = 2;
    std::vector<unsigned char*> outs(bands, nullptr);
    std::vector<unsigned long> sizes(bands, 0);
    std::vector<int> heights(bands, 0);
    std::vector<std::thread> threads;
    int origin = 0;
    for (int band = 0; band < bands; ++band) {
        const int band_height = (band == bands - 1) ? (height - origin) : (height / bands);
        heights[band] = band_height;
        const uint8_t* source = gray + static_cast<std::size_t>(origin) * static_cast<std::size_t>(width);
        origin += band_height;
        threads.emplace_back([&, band, band_height, source]() {
            tjhandle encoder = tjInitCompress();
            tjCompress2(encoder, source, width, 0, band_height, TJPF_GRAY, &outs[band], &sizes[band], TJSAMP_GRAY,
                        quality, TJFLAG_FASTDCT);
            tjDestroy(encoder);
        });
    }
    for (std::thread& worker : threads) {
        worker.join();
    }
    std::vector<uint8_t> packed;
    packed.insert(packed.end(), {'S', 'T', '0', '1'});
    ethcam::append_u32(packed, static_cast<uint32_t>(bands));
    for (int band = 0; band < bands; ++band) {
        ethcam::append_u32(packed, static_cast<uint32_t>(heights[band]));
        ethcam::append_u32(packed, static_cast<uint32_t>(sizes[band]));
        if (outs[band] != nullptr && sizes[band] > 0) {
            packed.insert(packed.end(), outs[band], outs[band] + sizes[band]);
        }
        tjFree(outs[band]);
    }
    return packed;
}

void run_session(int fd, const AppConfig& config, UsbCamera& camera, SessionControl& control,
                 uint32_t session_id) {
    std::string error;
    if (!camera.open(error)) {
        log_line("Session " + std::to_string(session_id) + " camera open failed: " + error);
        send_error(fd, error);
        std::vector<uint8_t> end_payload;
        ethcam::append_u32(end_payload, session_id);
        ethcam::append_u32(end_payload, 0);
        ethcam::append_u64(end_payload, 0);
        send_message(fd, ethcam::kSessionEnd, end_payload);
        return;
    }

    struct CloseCamera {
        UsbCamera& camera;
        ~CloseCamera() { camera.close(); }
    } close_camera{camera};

    if (session_should_end(control, session_id)) {
        std::vector<uint8_t> end_payload;
        ethcam::append_u32(end_payload, session_id);
        ethcam::append_u32(end_payload, 0);
        ethcam::append_u64(end_payload, 0);
        send_message(fd, ethcam::kSessionEnd, end_payload);
        log_line("Session " + std::to_string(session_id) + " stopped before the first frame");
        return;
    }

    log_line("Session " + std::to_string(session_id) + " started");
    const auto started = std::chrono::steady_clock::now();
    uint32_t frame_index = 0;

    struct JpegFrame {
        std::vector<uint8_t> jpeg;
        int width = 0;
        int height = 0;
    };
    struct PixelFrame {
        std::vector<uint8_t> pixels;
        int width = 0;
        int height = 0;
    };
    std::mutex queue_mu;
    std::condition_variable queue_cv;
    std::deque<JpegFrame> camera_q;
    std::deque<PixelFrame> decoded_q;
    std::deque<PixelFrame> balanced_q;
    std::atomic<bool> pipe_stop{false};
    std::string pipe_error;
    double fpga_wait_sum_us = 0.0;
    double fpga_wait_max_us = 0.0;
    int fpga_wait_count = 0;

    // Camera reads never wait on the FPGA. The next USB frame is taken while
    // the previous grayscale frame is still inside the accelerator.
    std::thread capture_thread([&]() {
        while (control.alive.load() && g_stop == 0 && !pipe_stop.load()) {
            cv::Mat frame;
            std::string read_error;
            if (!camera.read(frame, read_error)) {
                std::lock_guard<std::mutex> lock(queue_mu);
                pipe_error = read_error;
                pipe_stop = true;
                queue_cv.notify_all();
                break;
            }
            JpegFrame slot;
            std::string encode_error;
            slot.width = frame.cols;
            slot.height = frame.rows;
            if (!take_camera_jpeg(frame, slot.jpeg, slot.width, slot.height)) {
                slot.jpeg = encode_jpeg(frame, config.jpeg_quality, encode_error);
            }
            if (slot.jpeg.empty() || slot.width <= 0 || slot.height <= 0) {
                continue;
            }
            static bool logged_first = false;
            if (!logged_first) {
                logged_first = true;
                log_line("Camera capture runs beside the FPGA, MJPEG " + std::to_string(slot.width) + "x" +
                         std::to_string(slot.height) + " bytes=" + std::to_string(slot.jpeg.size()));
            }
            {
                std::unique_lock<std::mutex> lock(queue_mu);
                queue_cv.wait(lock, [&]() { return camera_q.size() < 6 || pipe_stop.load(); });
                if (pipe_stop.load()) {
                    break;
                }
                camera_q.push_back(std::move(slot));
            }
            queue_cv.notify_all();
        }
        pipe_stop = true;
        queue_cv.notify_all();
    });

    std::thread decode_thread([&]() {
        tjhandle decoder = tjInitDecompress();
        while (true) {
            JpegFrame slot;
            {
                std::unique_lock<std::mutex> lock(queue_mu);
                queue_cv.wait(lock, [&]() {
                    return !camera_q.empty() || pipe_stop.load();
                });
                if (camera_q.empty()) {
                    break;
                }
                slot = std::move(camera_q.front());
                camera_q.pop_front();
            }
            queue_cv.notify_all();
            PixelFrame gray;
            gray.width = slot.width;
            gray.height = slot.height;
            gray.pixels.resize(static_cast<std::size_t>(slot.width) * static_cast<std::size_t>(slot.height));
            if (tjDecompress2(decoder, slot.jpeg.data(), static_cast<unsigned long>(slot.jpeg.size()),
                              gray.pixels.data(), slot.width, 0, slot.height, TJPF_GRAY,
                              TJFLAG_FASTDCT | TJFLAG_FASTUPSAMPLE) != 0) {
                continue;
            }
            {
                std::unique_lock<std::mutex> lock(queue_mu);
                queue_cv.wait(lock, [&]() { return decoded_q.size() < 2 || pipe_stop.load(); });
                if (pipe_stop.load() && decoded_q.size() >= 2) {
                    break;
                }
                decoded_q.push_back(std::move(gray));
            }
            queue_cv.notify_all();
        }
        tjDestroy(decoder);
        queue_cv.notify_all();
    });

    std::thread fpga_thread([&]() {
        FpgaTile fpga;
        std::string open_error;
        if (!fpga.open(open_error)) {
            log_line(open_error);
            std::lock_guard<std::mutex> lock(queue_mu);
            pipe_error = open_error;
            pipe_stop = true;
            queue_cv.notify_all();
            return;
        }
        while (true) {
            PixelFrame slot;
            {
                std::unique_lock<std::mutex> lock(queue_mu);
                queue_cv.wait(lock, [&]() { return !decoded_q.empty() || pipe_stop.load(); });
                if (decoded_q.empty()) {
                    break;
                }
                slot = std::move(decoded_q.front());
                decoded_q.pop_front();
            }
            queue_cv.notify_all();
            cv::Mat gray(slot.height, slot.width, CV_8UC1, slot.pixels.data());
            cv::Mat processed;
            std::uint32_t cycles = 0;
            double wait_us = 0.0;
            std::string run_error;
            if (!fpga.process(gray, processed, config.tile_width, config.tile_height, cycles, wait_us, run_error) ||
                cycles == 0 || processed.empty()) {
                std::lock_guard<std::mutex> lock(queue_mu);
                pipe_error = run_error.empty() ? "FPGA tile core returned no result" : run_error;
                pipe_stop = true;
                queue_cv.notify_all();
                break;
            }
            PixelFrame out;
            out.width = processed.cols;
            out.height = processed.rows;
            out.pixels.assign(processed.data, processed.data + static_cast<std::size_t>(out.width) * out.height);
            {
                std::lock_guard<std::mutex> lock(queue_mu);
                fpga_wait_sum_us += wait_us;
                if (wait_us > fpga_wait_max_us) {
                    fpga_wait_max_us = wait_us;
                }
                ++fpga_wait_count;
                balanced_q.push_back(std::move(out));
            }
            queue_cv.notify_all();
        }
    });

    while (control.alive.load() && g_stop == 0) {
        if (session_should_end(control, session_id)) {
            break;
        }
        PixelFrame slot;
        {
            std::unique_lock<std::mutex> lock(queue_mu);
            queue_cv.wait_for(lock, std::chrono::milliseconds(50), [&]() {
                return !balanced_q.empty() ||
                       (pipe_stop.load() && camera_q.empty() && decoded_q.empty() && balanced_q.empty());
            });
            if (balanced_q.empty()) {
                if (pipe_stop.load() && camera_q.empty() && decoded_q.empty()) {
                    break;
                }
                continue;
            }
            slot = std::move(balanced_q.front());
            balanced_q.pop_front();
        }
        queue_cv.notify_all();
        const std::vector<uint8_t> packed =
            encode_strips(slot.pixels.data(), slot.width, slot.height, config.jpeg_quality);
        const auto timestamp_us = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());
        const std::vector<uint8_t> payload = frame_payload(session_id, frame_index, timestamp_us, slot.width,
                                                           slot.height, packed, ethcam::kFormatStrips);
        if (!send_message(fd, ethcam::kFrame, payload)) {
            control.alive = false;
            break;
        }
        ++frame_index;
        if (frame_index % 120 == 0) {
            double mean_us = 0.0;
            double max_us = 0.0;
            {
                std::lock_guard<std::mutex> lock(queue_mu);
                mean_us = fpga_wait_count > 0 ? fpga_wait_sum_us / fpga_wait_count : 0.0;
                max_us = fpga_wait_max_us;
                fpga_wait_sum_us = 0.0;
                fpga_wait_max_us = 0.0;
                fpga_wait_count = 0;
            }
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            const double actual_fps = seconds > 0.0 ? static_cast<double>(frame_index) / seconds : 0.0;
            log_line("frame " + std::to_string(frame_index) + " fpga wait mean " +
                     std::to_string(static_cast<int>(mean_us)) + " us, max " + std::to_string(static_cast<int>(max_us)) +
                     " us; sent at " + std::to_string(actual_fps) + " fps");
        }
    }
    pipe_stop = true;
    queue_cv.notify_all();
    capture_thread.join();
    decode_thread.join();
    fpga_thread.join();
    if (!pipe_error.empty()) {
        log_line(pipe_error);
        send_error(fd, pipe_error);
    }

    const auto duration_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started)
            .count());
    std::vector<uint8_t> end_payload;
    ethcam::append_u32(end_payload, session_id);
    ethcam::append_u32(end_payload, frame_index);
    ethcam::append_u64(end_payload, duration_us);
    send_message(fd, ethcam::kSessionEnd, end_payload);
    log_line("Session " + std::to_string(session_id) + " ended, frames=" + std::to_string(frame_index));
}

void serve_client(int fd, const AppConfig& config, UsbCamera& camera) {
    if (!send_message(fd, ethcam::kHello, hello_payload(config))) {
        return;
    }

    SessionControl control;
    std::thread reader(reader_loop, fd, std::ref(control));

    while (control.alive.load() && g_stop == 0) {
        Command command{};
        if (!take_next_command(control, command)) {
            break;
        }
        if (command.type == CommandType::Start && command.session_id == 0) {
            send_error(fd, "session_id must be non-zero");
            continue;
        }
        if (command.type == CommandType::Start) {
            run_session(fd, config, camera, control, command.session_id);
        }
    }

    control.alive = false;
    ::shutdown(fd, SHUT_RDWR);
    control.cv.notify_all();
    reader.join();
}

std::string peer_text(int fd) {
    sockaddr_storage address{};
    socklen_t length = sizeof(address);
    if (getpeername(fd, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        return "unknown";
    }
    char host[NI_MAXHOST];
    char service[NI_MAXSERV];
    if (getnameinfo(reinterpret_cast<sockaddr*>(&address), length, host, sizeof(host), service,
                    sizeof(service), NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
        return "unknown";
    }
    return std::string(host) + ":" + service;
}

}  // namespace

EthCameraServer::EthCameraServer(AppConfig config)
    : config_(std::move(config)), camera_(config_) {}

int EthCameraServer::run() {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    std::signal(SIGPIPE, SIG_IGN);

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    addrinfo* addresses = nullptr;
    const std::string port_text = std::to_string(config_.port);
    const int lookup = getaddrinfo(config_.bind_address.c_str(), port_text.c_str(), &hints, &addresses);
    if (lookup != 0) {
        log_line(std::string("getaddrinfo: ") + gai_strerror(lookup));
        return 1;
    }

    int listen_fd = -1;
    for (addrinfo* it = addresses; it != nullptr; it = it->ai_next) {
        listen_fd = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (listen_fd < 0) {
            continue;
        }
        const int enabled = 1;
        ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
        if (::bind(listen_fd, it->ai_addr, it->ai_addrlen) == 0) {
            break;
        }
        ::close(listen_fd);
        listen_fd = -1;
    }
    freeaddrinfo(addresses);

    if (listen_fd < 0) {
        log_line("Cannot bind " + config_.bind_address + ":" + port_text);
        return 1;
    }
    if (::listen(listen_fd, 1) != 0) {
        log_line("listen failed");
        ::close(listen_fd);
        return 1;
    }

    g_listen_fd = listen_fd;
    log_line("Board " + config_.board_name + ", Ubuntu " + config_.ubuntu_version);
    log_line("Ethernet camera listening on " + config_.bind_address + ":" + port_text +
             " for " + config_.camera_model);

    while (g_stop == 0) {
        sockaddr_storage client_address{};
        socklen_t client_length = sizeof(client_address);
        const int client_fd =
            ::accept(listen_fd, reinterpret_cast<sockaddr*>(&client_address), &client_length);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        const int enabled = 1;
        ::setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled));
        const int send_buffer = 4 * 1024 * 1024;
        ::setsockopt(client_fd, SOL_SOCKET, SO_SNDBUF, &send_buffer, sizeof(send_buffer));
        g_client_fd = client_fd;
        log_line("Client connected " + peer_text(client_fd));
        serve_client(client_fd, config_, camera_);
        log_line("Client disconnected " + peer_text(client_fd));
        g_client_fd = -1;
        ::close(client_fd);
        camera_.close();
    }

    g_listen_fd = -1;
    ::close(listen_fd);
    log_line("Stopped");
    return 0;
}
