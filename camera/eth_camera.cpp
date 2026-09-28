// Program 1: get a frame, then decode it, then serve an Ethernet camera.
// On the Kria the same role is kria/build/kria_eth_camera (USB OV9281).
// On this PC it opens the laptop webcam and listens on 127.0.0.1:5600.
// The C# viewer only connects and displays the JPEG frames.
//
//   g++ -O2 -std=c++17 -I../kria/include eth_camera.cpp -o eth_camera ^
//       -lole32 -loleaut32 -lstrmiids -luuid -lgdiplus -lws2_32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "config.hpp"
#include "protocol.hpp"
#include "../fpga/hls/tile_brightness_core.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <unknwn.h>
#include <ole2.h>
#include <gdiplus.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "strmiids.lib")
#pragma comment(lib, "gdiplus.lib")

namespace {

const GUID kFilterGraph = {0xe436ebb3, 0x524f, 0x11ce, {0x9f, 0x53, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}};
const GUID kGraphBuilderId = {0x56a868a9, 0x0ad4, 0x11ce, {0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}};
const GUID kMediaControlId = {0x56a868b1, 0x0ad4, 0x11ce, {0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}};
const GUID kCaptureBuilder = {0xbf87b6e1, 0x8c27, 0x11d0, {0xb3, 0xf0, 0x00, 0xaa, 0x00, 0x37, 0x61, 0xc5}};
const GUID kCaptureBuilderId = {0x93e5a4e0, 0x2d50, 0x11d2, {0xab, 0xfa, 0x00, 0xa0, 0xc9, 0xc6, 0xe3, 0x8d}};
const GUID kDeviceEnum = {0x62be8d1a, 0x11fb, 0x11d0, {0x90, 0xe5, 0x00, 0xc0, 0x4f, 0xd6, 0xdd, 0xc1}};
const GUID kVideoInput = {0x860bb310, 0x5d01, 0x11d0, {0xbd, 0x3b, 0x00, 0xa0, 0xc9, 0x11, 0xce, 0x86}};
const GUID kCreateDevEnumId = {0x29840822, 0x5b84, 0x11d0, {0xbd, 0x3b, 0x00, 0xa0, 0xc9, 0x11, 0xce, 0x86}};
const GUID kSampleGrabber = {0xC1F400A0, 0x3F08, 0x11d3, {0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37}};
const GUID kNullRenderer = {0xC1F400A4, 0x3F08, 0x11d3, {0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37}};
const GUID kSampleGrabberId = {0x6B652FFF, 0x11FE, 0x4fce, {0x92, 0xAD, 0x02, 0x66, 0xB5, 0xD7, 0xC7, 0x8F}};
const GUID kSampleGrabberCbId = {0x0579154A, 0x2B53, 0x4994, {0xB0, 0xD0, 0xE7, 0x73, 0x14, 0x8E, 0xFF, 0x85}};
const GUID kMediaVideo = {0x73646976, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
const GUID kRgb24 = {0xe436eb7d, 0x524f, 0x11ce, {0x9f, 0x53, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}};
const GUID kVideoInfo = {0x05589f80, 0xc356, 0x11ce, {0xbf, 0x01, 0x00, 0xaa, 0x00, 0x55, 0x59, 0x5a}};
const GUID kPinCapture = {0xfb6c4281, 0x0353, 0x11d1, {0x90, 0x5f, 0x00, 0x00, 0xc0, 0xcc, 0x16, 0xba}};

struct AM_MEDIA_TYPE {
    GUID majortype;
    GUID subtype;
    BOOL fixed_size;
    BOOL temporal;
    ULONG sample_size;
    GUID formattype;
    IUnknown* punk;
    ULONG format_size;
    BYTE* format;
};

struct VIDEOINFOHEADER {
    RECT source;
    RECT target;
    DWORD bit_rate;
    DWORD bit_error_rate;
    LONGLONG average_time;
    BITMAPINFOHEADER bmi;
};

struct ISampleGrabberCB : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SampleCB(double sample_time, void* sample) = 0;
    virtual HRESULT STDMETHODCALLTYPE BufferCB(double sample_time, BYTE* buffer, long length) = 0;
};

struct ISampleGrabber : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SetOneShot(BOOL one_shot) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetMediaType(const AM_MEDIA_TYPE* type) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetConnectedMediaType(AM_MEDIA_TYPE* type) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetBufferSamples(BOOL buffer_them) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentBuffer(long* buffer_size, long* buffer) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentSample(void** sample) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCallback(ISampleGrabberCB* callback, long which) = 0;
};

struct IDispatch : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* count) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT index, LCID locale, void** info) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID riid, LPOLESTR* names, UINT count, LCID locale, LONG* ids) = 0;
    virtual HRESULT STDMETHODCALLTYPE Invoke(LONG id, REFIID riid, LCID locale, WORD flags, void* params, void* result,
                                             void* error, UINT* arg) = 0;
};

struct IMediaControl : IDispatch {
    virtual HRESULT STDMETHODCALLTYPE Run() = 0;
    virtual HRESULT STDMETHODCALLTYPE Pause() = 0;
    virtual HRESULT STDMETHODCALLTYPE Stop() = 0;
};

struct IFilterGraph : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE AddFilter(IUnknown* filter, LPCWSTR name) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveFilter(IUnknown* filter) = 0;
    virtual HRESULT STDMETHODCALLTYPE EnumFilters(void** filters) = 0;
    virtual HRESULT STDMETHODCALLTYPE FindFilterByName(LPCWSTR name, void** filter) = 0;
    virtual HRESULT STDMETHODCALLTYPE ConnectDirect(void* out_pin, void* in_pin, const AM_MEDIA_TYPE* type) = 0;
    virtual HRESULT STDMETHODCALLTYPE Reconnect(void* pin) = 0;
    virtual HRESULT STDMETHODCALLTYPE Disconnect(void* pin) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultSyncSource() = 0;
};

struct IGraphBuilder : IFilterGraph {
    virtual HRESULT STDMETHODCALLTYPE Connect(void* out_pin, void* in_pin) = 0;
    virtual HRESULT STDMETHODCALLTYPE Render(void* pin) = 0;
    virtual HRESULT STDMETHODCALLTYPE RenderFile(LPCWSTR file, LPCWSTR playlist) = 0;
    virtual HRESULT STDMETHODCALLTYPE AddSourceFilter(LPCWSTR file, LPCWSTR name, void** filter) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetLogFile(DWORD_PTR file) = 0;
    virtual HRESULT STDMETHODCALLTYPE Abort() = 0;
    virtual HRESULT STDMETHODCALLTYPE ShouldOperationContinue() = 0;
};

struct ICaptureGraphBuilder2 : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SetFiltergraph(IGraphBuilder* graph) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFiltergraph(IGraphBuilder** graph) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetOutputFileName(const GUID* type, LPCOLESTR file, IUnknown** mux, void** sink) = 0;
    virtual HRESULT STDMETHODCALLTYPE FindInterface(const GUID* category, const GUID* type, IUnknown* filter, REFIID iid,
                                                    void** found) = 0;
    virtual HRESULT STDMETHODCALLTYPE RenderStream(const GUID* category, const GUID* type, IUnknown* source,
                                                   IUnknown* intermediate, IUnknown* sink) = 0;
};

struct ICreateDevEnum : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CreateClassEnumerator(REFCLSID category, IEnumMoniker** monikers, DWORD flags) = 0;
};

struct RawFrame {
    std::vector<uint8_t> bgr;
    int width = 0;
    int height = 0;
};

struct JpegFrame {
    std::vector<uint8_t> jpeg;
    int width = 0;
    int height = 0;
};

CRITICAL_SECTION g_raw_mu;
std::deque<RawFrame> g_raw;
std::atomic<bool> g_run{true};
std::atomic<long long> g_got{0};
std::atomic<long long> g_decoded{0};

int g_width = 0;
int g_height = 0;
int g_stride = 0;
bool g_bottom_up = true;
std::vector<uint8_t> g_jpeg;
int g_jpeg_width = 0;
int g_jpeg_height = 0;
uint64_t g_jpeg_serial = 0;
AppConfig g_config;

void log_line(const std::string& text) {
    std::cerr << "[eth_camera] " << text << std::endl;
}

class GrabCallback : public ISampleGrabberCB {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** object) override {
        if (riid == IID_IUnknown || riid == kSampleGrabberCbId) {
            *object = static_cast<ISampleGrabberCB*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }
    STDMETHODIMP SampleCB(double, void*) override { return S_OK; }
    STDMETHODIMP BufferCB(double, BYTE* buffer, long length) override {
        if (buffer == nullptr || g_width <= 0 || g_height <= 0 || length <= 0) {
            return S_OK;
        }
        RawFrame frame;
        frame.width = g_width;
        frame.height = g_height;
        frame.bgr.resize(static_cast<std::size_t>(g_width) * static_cast<std::size_t>(g_height) * 3u);
        const int stride = g_stride > 0 ? g_stride : g_width * 3;
        for (int y = 0; y < g_height; ++y) {
            const int source_y = g_bottom_up ? (g_height - 1 - y) : y;
            const uint8_t* row = buffer + static_cast<std::size_t>(source_y) * static_cast<std::size_t>(stride);
            std::memcpy(frame.bgr.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(g_width) * 3u, row,
                        static_cast<std::size_t>(g_width) * 3u);
        }
        {
            EnterCriticalSection(&g_raw_mu);
            if (g_raw.size() >= 2) {
                g_raw.pop_front();
            }
            g_raw.push_back(std::move(frame));
            LeaveCriticalSection(&g_raw_mu);
        }
        g_got.fetch_add(1);
        return S_OK;
    }
};

ULONG_PTR g_gdi = 0;

bool jpeg_encoder(CLSID* clsid) {
    UINT count = 0;
    UINT bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || bytes == 0) {
        return false;
    }
    std::vector<uint8_t> storage(bytes);
    auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(storage.data());
    if (Gdiplus::GetImageEncoders(count, bytes, codecs) != Gdiplus::Ok) {
        return false;
    }
    for (UINT index = 0; index < count; ++index) {
        if (std::wcscmp(codecs[index].MimeType, L"image/jpeg") == 0) {
            *clsid = codecs[index].Clsid;
            return true;
        }
    }
    return false;
}

bool encode_jpeg(const RawFrame& frame, std::vector<uint8_t>& jpeg) {
    Gdiplus::Bitmap bitmap(frame.width, frame.height, frame.width * 3, PixelFormat24bppRGB,
                           const_cast<BYTE*>(frame.bgr.data()));
    CLSID clsid{};
    if (!jpeg_encoder(&clsid)) {
        return false;
    }
    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(nullptr, TRUE, &stream) != S_OK) {
        return false;
    }
    ULONG quality = static_cast<ULONG>(g_config.jpeg_quality);
    Gdiplus::EncoderParameters parameters;
    parameters.Count = 1;
    parameters.Parameter[0].Guid = Gdiplus::EncoderQuality;
    parameters.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
    parameters.Parameter[0].NumberOfValues = 1;
    parameters.Parameter[0].Value = &quality;
    const auto status = bitmap.Save(stream, &clsid, &parameters);
    HGLOBAL memory = nullptr;
    GetHGlobalFromStream(stream, &memory);
    const auto* bytes = static_cast<const uint8_t*>(GlobalLock(memory));
    const auto size = GlobalSize(memory);
    const bool ok = status == Gdiplus::Ok && bytes != nullptr && size > 0;
    if (ok) {
        jpeg.assign(bytes, bytes + size);
    }
    GlobalUnlock(memory);
    stream->Release();
    return ok;
}

void decode_loop() {
    while (g_run.load()) {
        RawFrame frame;
        {
            EnterCriticalSection(&g_raw_mu);
            if (g_raw.empty()) {
                LeaveCriticalSection(&g_raw_mu);
                if (!g_run.load()) {
                    break;
                }
                Sleep(1);
                continue;
            }
            frame = std::move(g_raw.front());
            g_raw.pop_front();
            LeaveCriticalSection(&g_raw_mu);
        }
        std::vector<uint8_t> jpeg;
        const int pixels = frame.width * frame.height;
        std::vector<uint8_t> gray(static_cast<std::size_t>(pixels));
        std::vector<uint8_t> shaded(gray.size());
        for (int index = 0; index < pixels; ++index) {
            const uint8_t* bgr = frame.bgr.data() + static_cast<std::size_t>(index) * 3u;
            gray[static_cast<std::size_t>(index)] =
                static_cast<uint8_t>((29u * bgr[0] + 150u * bgr[1] + 77u * bgr[2]) >> 8);
        }
        shade_image(gray.data(), shaded.data(), frame.width, frame.height);
        for (int index = 0; index < pixels; ++index) {
            uint8_t* bgr = frame.bgr.data() + static_cast<std::size_t>(index) * 3u;
            bgr[0] = bgr[1] = bgr[2] = shaded[static_cast<std::size_t>(index)];
        }
        if (!encode_jpeg(frame, jpeg)) {
            continue;
        }
        g_decoded.fetch_add(1);
        {
            EnterCriticalSection(&g_raw_mu);
            g_jpeg = std::move(jpeg);
            g_jpeg_width = frame.width;
            g_jpeg_height = frame.height;
            g_jpeg_serial++;
            LeaveCriticalSection(&g_raw_mu);
        }
    }
}

const GUID kBaseFilterId = {0x56a86895, 0x0ad4, 0x11ce, {0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}};

IUnknown* bind_camera() {
    ICreateDevEnum* devices = nullptr;
    const HRESULT created = CoCreateInstance(kDeviceEnum, nullptr, CLSCTX_INPROC_SERVER, kCreateDevEnumId,
                                             reinterpret_cast<void**>(&devices));
    if (created != S_OK) {
        log_line("Device enumerator failed " + std::to_string(static_cast<unsigned long>(created)));
        return nullptr;
    }
    IEnumMoniker* monikers = nullptr;
    const HRESULT listed = devices->CreateClassEnumerator(kVideoInput, &monikers, 0);
    devices->Release();
    if (listed != S_OK || monikers == nullptr) {
        log_line("No video capture category");
        return nullptr;
    }
    IMoniker* moniker = nullptr;
    IUnknown* filter = nullptr;
    if (monikers->Next(1, &moniker, nullptr) == S_OK) {
        const HRESULT bound = moniker->BindToObject(nullptr, nullptr, kBaseFilterId, reinterpret_cast<void**>(&filter));
        if (bound != S_OK) {
            log_line("Cannot open the camera filter");
        }
        moniker->Release();
    } else {
        log_line("Camera list is empty");
    }
    monikers->Release();
    return filter;
}

void free_media_type(AM_MEDIA_TYPE* type) {
    if (type->format_size != 0) {
        CoTaskMemFree(type->format);
        type->format_size = 0;
        type->format = nullptr;
    }
    if (type->punk != nullptr) {
        type->punk->Release();
        type->punk = nullptr;
    }
}

bool start_camera(IMediaControl** control_out, GrabCallback* callback) {
    IGraphBuilder* graph = nullptr;
    ICaptureGraphBuilder2* capture = nullptr;
    IUnknown* source = bind_camera();
    IUnknown* grabber_filter = nullptr;
    ISampleGrabber* grabber = nullptr;
    IUnknown* null_renderer = nullptr;
    IMediaControl* control = nullptr;
    bool started = false;

    if (source == nullptr) {
        log_line("No laptop camera");
        return false;
    }
    if (CoCreateInstance(kFilterGraph, nullptr, CLSCTX_INPROC_SERVER, kGraphBuilderId,
                         reinterpret_cast<void**>(&graph)) != S_OK ||
        CoCreateInstance(kCaptureBuilder, nullptr, CLSCTX_INPROC_SERVER, kCaptureBuilderId,
                         reinterpret_cast<void**>(&capture)) != S_OK ||
        capture->SetFiltergraph(graph) != S_OK || graph->AddFilter(source, L"Camera") != S_OK) {
        log_line("Cannot build the capture graph");
        goto done;
    }
    if (CoCreateInstance(kSampleGrabber, nullptr, CLSCTX_INPROC_SERVER, kBaseFilterId,
                         reinterpret_cast<void**>(&grabber_filter)) != S_OK ||
        grabber_filter->QueryInterface(kSampleGrabberId, reinterpret_cast<void**>(&grabber)) != S_OK) {
        log_line("Sample grabber is missing");
        goto done;
    }
    {
        AM_MEDIA_TYPE wanted{};
        wanted.majortype = kMediaVideo;
        wanted.subtype = kRgb24;
        wanted.formattype = kVideoInfo;
        grabber->SetMediaType(&wanted);
    }
    grabber->SetOneShot(FALSE);
    grabber->SetBufferSamples(FALSE);
    graph->AddFilter(grabber_filter, L"Grabber");
    if (CoCreateInstance(kNullRenderer, nullptr, CLSCTX_INPROC_SERVER, kBaseFilterId,
                         reinterpret_cast<void**>(&null_renderer)) != S_OK ||
        graph->AddFilter(null_renderer, L"Null") != S_OK) {
        goto done;
    }
    if (capture->RenderStream(&kPinCapture, &kMediaVideo, source, grabber_filter, null_renderer) != S_OK) {
        log_line("Cannot connect the laptop camera");
        goto done;
    }
    grabber->SetCallback(callback, 1);
    {
        AM_MEDIA_TYPE connected{};
        if (grabber->GetConnectedMediaType(&connected) == S_OK && connected.format != nullptr) {
            const auto* info = reinterpret_cast<VIDEOINFOHEADER*>(connected.format);
            g_width = info->bmi.biWidth;
            g_height = std::abs(info->bmi.biHeight);
            g_bottom_up = info->bmi.biHeight > 0;
            g_stride = g_height > 0 && info->bmi.biSizeImage > 0
                           ? static_cast<int>(info->bmi.biSizeImage / static_cast<DWORD>(g_height))
                           : ((g_width * 3 + 3) & ~3);
            free_media_type(&connected);
        }
    }
    if (graph->QueryInterface(kMediaControlId, reinterpret_cast<void**>(&control)) != S_OK || control->Run() != S_OK) {
        log_line("Cannot start the laptop camera");
        goto done;
    }
    *control_out = control;
    control = nullptr;
    started = true;
    log_line("Laptop camera " + std::to_string(g_width) + "x" + std::to_string(g_height));

done:
    if (control != nullptr) {
        control->Release();
    }
    if (null_renderer != nullptr) {
        null_renderer->Release();
    }
    if (grabber != nullptr) {
        grabber->Release();
    }
    if (grabber_filter != nullptr) {
        grabber_filter->Release();
    }
    if (source != nullptr) {
        source->Release();
    }
    if (capture != nullptr) {
        capture->Release();
    }
    if (graph != nullptr) {
        graph->Release();
    }
    return started;
}

bool send_all(SOCKET socket, const uint8_t* data, std::size_t size) {
    std::size_t sent = 0;
    while (sent < size) {
        const int wrote = ::send(socket, reinterpret_cast<const char*>(data + sent), static_cast<int>(size - sent), 0);
        if (wrote <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(wrote);
    }
    return true;
}

bool recv_all(SOCKET socket, uint8_t* data, std::size_t size) {
    std::size_t got = 0;
    while (got < size) {
        const int read = ::recv(socket, reinterpret_cast<char*>(data + got), static_cast<int>(size - got), 0);
        if (read <= 0) {
            return false;
        }
        got += static_cast<std::size_t>(read);
    }
    return true;
}

bool send_message(SOCKET socket, uint16_t type, const std::vector<uint8_t>& payload) {
    const auto message = ethcam::encode_message(type, payload);
    return send_all(socket, message.data(), message.size());
}

std::vector<uint8_t> frame_payload(uint32_t session_id, uint32_t index, int width, int height,
                                   const std::vector<uint8_t>& jpeg) {
    std::vector<uint8_t> payload;
    ethcam::append_u32(payload, session_id);
    ethcam::append_u32(payload, index);
    const auto stamp = std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    ethcam::append_u64(payload, static_cast<uint64_t>(stamp));
    ethcam::append_u32(payload, static_cast<uint32_t>(width));
    ethcam::append_u32(payload, static_cast<uint32_t>(height));
    ethcam::append_u32(payload, ethcam::kFormatJpeg);
    ethcam::append_u32(payload, static_cast<uint32_t>(jpeg.size()));
    payload.insert(payload.end(), jpeg.begin(), jpeg.end());
    return payload;
}

void serve(SOCKET client) {
    std::vector<uint8_t> hello;
    ethcam::append_str(hello, g_config.board_name);
    ethcam::append_str(hello, g_config.ubuntu_version);
    ethcam::append_str(hello, g_config.camera_model);
    ethcam::append_u32(hello, static_cast<uint32_t>(g_width > 0 ? g_width : g_config.width));
    ethcam::append_u32(hello, static_cast<uint32_t>(g_height > 0 ? g_height : g_config.height));
    ethcam::append_u32(hello, static_cast<uint32_t>(g_config.fps));
    if (!send_message(client, ethcam::kHello, hello)) {
        return;
    }

    uint32_t session_id = 0;
    uint32_t frame_index = 0;
    uint64_t last_serial = 0;
    const auto started = std::chrono::steady_clock::now();
    while (g_run.load()) {
        uint8_t header[12];
        timeval wait{};
        wait.tv_sec = 0;
        wait.tv_usec = 30000;
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(client, &read_set);
        const int ready = ::select(0, &read_set, nullptr, nullptr, &wait);
        if (ready > 0) {
            if (!recv_all(client, header, sizeof(header)) || !ethcam::magic_ok(header)) {
                break;
            }
            const uint16_t type = ethcam::load_u16(header + 6);
            const uint32_t length = ethcam::load_u32(header + 8);
            std::vector<uint8_t> payload(length);
            if (length > 0 && !recv_all(client, payload.data(), length)) {
                break;
            }
            if (type == ethcam::kStart && payload.size() >= 4) {
                session_id = ethcam::load_u32(payload.data());
                frame_index = 0;
                log_line("session " + std::to_string(session_id));
            } else if (type == ethcam::kStop && session_id != 0) {
                const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                                         std::chrono::steady_clock::now() - started)
                                         .count();
                std::vector<uint8_t> end;
                ethcam::append_u32(end, session_id);
                ethcam::append_u32(end, frame_index);
                ethcam::append_u64(end, static_cast<uint64_t>(elapsed));
                send_message(client, ethcam::kSessionEnd, end);
                session_id = 0;
            }
        }

        if (session_id == 0) {
            continue;
        }
        std::vector<uint8_t> jpeg;
        int width = 0;
        int height = 0;
        uint64_t serial = 0;
        {
            EnterCriticalSection(&g_raw_mu);
            serial = g_jpeg_serial;
            if (serial != last_serial && !g_jpeg.empty()) {
                jpeg = g_jpeg;
                width = g_jpeg_width;
                height = g_jpeg_height;
            }
            LeaveCriticalSection(&g_raw_mu);
        }
        if (jpeg.empty()) {
            continue;
        }
        last_serial = serial;
        if (!send_message(client, ethcam::kFrame, frame_payload(session_id, frame_index, width, height, jpeg))) {
            break;
        }
        ++frame_index;
        if (frame_index % 30 == 0) {
            log_line("sent " + std::to_string(frame_index) + " got " + std::to_string(g_got.load()) + " decoded " +
                     std::to_string(g_decoded.load()));
        }
    }
}

bool recv_exact(SOCKET socket, uint8_t* data, std::size_t size) {
    std::size_t got = 0;
    while (got < size) {
        const int read = ::recv(socket, reinterpret_cast<char*>(data + got), static_cast<int>(size - got), 0);
        if (read <= 0) {
            return false;
        }
        got += static_cast<std::size_t>(read);
    }
    return true;
}

DWORD WINAPI raw_feed_thread(void*) {
    const SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(5601);
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || ::listen(listener, 1) != 0) {
        log_line("Cannot listen for the camera feed on 127.0.0.1:5601");
        return 1;
    }
    log_line("Waiting for raw camera frames on 127.0.0.1:5601");
    const SOCKET source = ::accept(listener, nullptr, nullptr);
    if (source == INVALID_SOCKET) {
        ::closesocket(listener);
        return 1;
    }
    while (g_run.load()) {
        uint8_t header[8];
        if (!recv_exact(source, header, sizeof(header))) {
            break;
        }
        const int width = static_cast<int>(ethcam::load_u32(header));
        const int height = static_cast<int>(ethcam::load_u32(header + 4));
        if (width < 1 || height < 1 || width > kWidth || height > kHeight) {
            break;
        }
        std::vector<uint8_t> gray(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
        if (!recv_exact(source, gray.data(), gray.size())) {
            break;
        }
        RawFrame frame;
        frame.width = width;
        frame.height = height;
        frame.bgr.resize(gray.size() * 3u);
        for (std::size_t index = 0; index < gray.size(); ++index) {
            frame.bgr[index * 3u] = frame.bgr[index * 3u + 1u] = frame.bgr[index * 3u + 2u] = gray[index];
        }
        g_width = width;
        g_height = height;
        EnterCriticalSection(&g_raw_mu);
        if (g_raw.size() >= 2) {
            g_raw.pop_front();
        }
        g_raw.push_back(std::move(frame));
        LeaveCriticalSection(&g_raw_mu);
        g_got.fetch_add(1);
    }
    ::closesocket(source);
    ::closesocket(listener);
    return 0;
}

DWORD WINAPI decode_thread(void*) {
    decode_loop();
    return 0;
}

}  // namespace

void load_runtime_config(int argc, char** argv) {
    std::string path;
    for (int index = 1; index < argc; ++index) {
        if (std::string(argv[index]) == "--config" && index + 1 < argc) {
            path = argv[++index];
        }
    }
    if (path.empty()) {
        const char* candidates[] = {"config/laptop.conf", "../config/laptop.conf", "../../config/laptop.conf"};
        for (const char* candidate : candidates) {
            std::ifstream probe(candidate);
            if (probe) {
                path = candidate;
                break;
            }
        }
    }
    if (path.empty() || !g_config.load_file(path)) {
        g_config.mode = "laptop";
        g_config.board_name = "Laptop";
        g_config.ubuntu_version = "Windows";
        g_config.camera_model = "LG Camera";
        g_config.pixel_format = "YUY2";
        g_config.bind_address = "127.0.0.1";
        g_config.host = "127.0.0.1";
        g_config.port = 5600;
        g_config.width = 640;
        g_config.height = 480;
        g_config.fps = 30;
        g_config.sanitize();
        log_line("No laptop.conf, using the laptop defaults");
        return;
    }
    log_line("Loaded " + path + " mode=" + g_config.mode + " " + g_config.pixel_format + " " +
             std::to_string(g_config.width) + "x" + std::to_string(g_config.height) + " @" +
             std::to_string(g_config.fps));
}

int main(int argc, char** argv) {
    load_runtime_config(argc, argv);
    Gdiplus::GdiplusStartupInput gdi_input;
    if (Gdiplus::GdiplusStartup(&g_gdi, &gdi_input, nullptr) != Gdiplus::Ok) {
        log_line("GDI+ failed");
        return 1;
    }
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        log_line("COM failed");
        return 1;
    }
    WSADATA sockets{};
    if (WSAStartup(MAKEWORD(2, 2), &sockets) != 0) {
        log_line("Winsock failed");
        return 1;
    }

    InitializeCriticalSection(&g_raw_mu);
    GrabCallback callback;
    IMediaControl* control = nullptr;
    HANDLE raw_feed = nullptr;
    if (!start_camera(&control, &callback)) {
        g_width = g_config.width;
        g_height = g_config.height;
        raw_feed = CreateThread(nullptr, 0, raw_feed_thread, nullptr, 0, nullptr);
        log_line("DirectShow camera unavailable, using the raw feed");
    }
    const HANDLE decoder = CreateThread(nullptr, 0, decode_thread, nullptr, 0, nullptr);

    const SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(g_config.port));
    address.sin_addr.s_addr = inet_addr(g_config.bind_address.c_str());
    int reuse = 1;
    ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    if (::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || ::listen(listener, 1) != 0) {
        log_line("Cannot listen on " + g_config.bind_address + ":" + std::to_string(g_config.port));
        g_run = false;
        WaitForSingleObject(decoder, INFINITE);
        if (control != nullptr) {
            control->Stop();
            control->Release();
        }
        return 1;
    }
    log_line("Ethernet camera on " + g_config.bind_address + ":" + std::to_string(g_config.port));
    const SOCKET client = ::accept(listener, nullptr, nullptr);
    if (client != INVALID_SOCKET) {
        serve(client);
        ::closesocket(client);
    }
    ::closesocket(listener);
    g_run = false;
    WaitForSingleObject(decoder, INFINITE);
    CloseHandle(decoder);
    DeleteCriticalSection(&g_raw_mu);
    if (control != nullptr) {
        control->Stop();
        control->Release();
    }
    if (raw_feed != nullptr) {
        WaitForSingleObject(raw_feed, 1000);
        CloseHandle(raw_feed);
    }
    WSACleanup();
    CoUninitialize();
    Gdiplus::GdiplusShutdown(g_gdi);
    return 0;
}
