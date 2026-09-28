# Two modes for the camera get + MJPEG decode pipeline.
#
#   python pc/mjpeg_pipeline.py --mode sim
#       Laptop. A fake OV9281 produces MJPEG. One thread reads frames and
#       another thread decodes them at the same time, using the same queue
#       limits as kria/src/eth_camera_server.cpp.
#
#   python pc/mjpeg_pipeline.py --mode board
#       Kria KV260. This laptop has no USB OV9281. The command prints how to
#       run the real camera binary on the board.

import argparse
import sys
import threading
import time
from collections import deque

import cv2
import numpy as np

WIDTH = 1280
HEIGHT = 800
FPS = 120
JPEG_QUALITY = 85
JPEG_QUEUE_LIMIT = 8
DECODED_QUEUE_LIMIT = 4
FRAME_COUNT = 24


def make_frame(index):
    image = np.empty((HEIGHT, WIDTH), dtype=np.uint8)
    level = 30 + (index * 7) % 180
    image[:] = level
    x0 = (index * 37) % (WIDTH - 80)
    image[80:200, x0 : x0 + 80] = 240
    image[index % HEIGHT, :] = 255
    return image


def encode_jpeg(image):
    ok, encoded = cv2.imencode(
        ".jpg", image, [int(cv2.IMWRITE_JPEG_QUALITY), JPEG_QUALITY]
    )
    if not ok:
        raise RuntimeError("JPEG encode failed")
    return encoded.tobytes()


def decode_jpeg(payload, width, height):
    buffer = np.frombuffer(payload, dtype=np.uint8)
    image = cv2.imdecode(buffer, cv2.IMREAD_GRAYSCALE)
    if image is None or image.shape != (height, width):
        return None
    return image


def run_sim():
    sources = [make_frame(index) for index in range(FRAME_COUNT)]
    payloads = [encode_jpeg(image) for image in sources]
    payloads.append(b"\xff\xd8not-a-jpeg")

    jpeg_mu = threading.Lock()
    jpeg_cv = threading.Condition(jpeg_mu)
    jpeg_q = deque()
    jpeg_inflight = 0

    decoded_mu = threading.Lock()
    decoded_cv = threading.Condition(decoded_mu)
    decoded_q = deque()

    stop = threading.Event()
    get_intervals = []
    decode_intervals = []
    max_jpeg_q = 0
    max_inflight = 0
    errors = []

    def note_bounds(queue_size, inflight):
        nonlocal max_jpeg_q, max_inflight
        max_jpeg_q = max(max_jpeg_q, queue_size)
        max_inflight = max(max_inflight, inflight)

    def get_thread():
        nonlocal jpeg_inflight
        for index, payload in enumerate(payloads):
            if stop.is_set():
                break
            started = time.perf_counter()
            # The fake camera already delivered one MJPEG buffer, same as V4L2 DQBUF.
            time.sleep(1.0 / FPS)
            slot = (index, WIDTH, HEIGHT, payload)
            get_intervals.append((started, time.perf_counter()))
            with jpeg_cv:
                jpeg_cv.wait_for(lambda: len(jpeg_q) < JPEG_QUEUE_LIMIT or stop.is_set())
                if stop.is_set():
                    break
                jpeg_q.append(slot)
                jpeg_inflight += 1
                note_bounds(len(jpeg_q), jpeg_inflight)
                jpeg_cv.notify_all()
        stop.set()
        with jpeg_cv:
            jpeg_cv.notify_all()
        with decoded_cv:
            decoded_cv.notify_all()

    def decode_thread():
        nonlocal jpeg_inflight
        while True:
            with jpeg_cv:
                jpeg_cv.wait_for(lambda: jpeg_q or stop.is_set())
                if not jpeg_q:
                    break
                index, width, height, payload = jpeg_q.popleft()
                note_bounds(len(jpeg_q), jpeg_inflight)
                jpeg_cv.notify_all()
            started = time.perf_counter()
            gray = decode_jpeg(payload, width, height)
            decode_intervals.append((started, time.perf_counter()))
            if gray is None:
                with jpeg_cv:
                    jpeg_inflight -= 1
                    note_bounds(len(jpeg_q), jpeg_inflight)
                with decoded_cv:
                    decoded_cv.notify_all()
                continue
            with decoded_cv:
                decoded_cv.wait_for(lambda: len(decoded_q) < DECODED_QUEUE_LIMIT or stop.is_set())
                if stop.is_set() and len(decoded_q) >= DECODED_QUEUE_LIMIT:
                    with jpeg_cv:
                        jpeg_inflight -= 1
                    decoded_cv.notify_all()
                    break
                decoded_q.append((index, gray))
                decoded_cv.notify_all()
            with jpeg_cv:
                jpeg_inflight -= 1
                note_bounds(len(jpeg_q), jpeg_inflight)
            with decoded_cv:
                decoded_cv.notify_all()

    def overlap_seconds():
        total = 0.0
        for get_start, get_end in get_intervals:
            for decode_start, decode_end in decode_intervals:
                total += max(0.0, min(get_end, decode_end) - max(get_start, decode_start))
        return total

    reader = threading.Thread(target=decode_thread, name="mjpeg-decode")
    getter = threading.Thread(target=get_thread, name="camera-get")
    reader.start()
    getter.start()

    received = []
    while True:
        with decoded_cv:
            ready = decoded_cv.wait_for(
                lambda: decoded_q or (stop.is_set() and jpeg_inflight == 0 and not jpeg_q),
                timeout=2.0,
            )
            if decoded_q:
                received.append(decoded_q.popleft())
                decoded_cv.notify_all()
                continue
            if ready and stop.is_set() and jpeg_inflight == 0:
                break
            if not ready and stop.is_set() and jpeg_inflight == 0 and not jpeg_q:
                break
            if not ready and not stop.is_set():
                errors.append("timed out waiting for a decoded frame")
                stop.set()
                break

    getter.join()
    reader.join()

    good_indexes = [index for index, image in received]
    if good_indexes != list(range(FRAME_COUNT)):
        errors.append(f"frame order {good_indexes} != 0..{FRAME_COUNT - 1}")

    abs_errors = []
    for index, image in received:
        if index >= FRAME_COUNT:
            errors.append(f"unexpected frame {index}")
            continue
        abs_errors.append(np.mean(np.abs(image.astype(np.int16) - sources[index].astype(np.int16))))
    mean_abs = float(np.mean(abs_errors)) if abs_errors else 999.0
    if mean_abs > 8.0:
        errors.append(f"mean absolute error {mean_abs:.2f} is above 8")

    overlapped = overlap_seconds()
    if overlapped <= 0.0:
        errors.append("get and decode never ran at the same time")
    if max_jpeg_q > JPEG_QUEUE_LIMIT:
        errors.append(f"jpeg queue grew to {max_jpeg_q}")

    elapsed = max(end for _, end in get_intervals + decode_intervals) - min(
        start for start, _ in get_intervals + decode_intervals
    )
    print(
        f"sim mode: {FRAME_COUNT} good frames + 1 corrupt JPEG, {WIDTH}x{HEIGHT}, quality {JPEG_QUALITY}"
    )
    print(
        f"get thread and decode thread overlapped {overlapped * 1000:.1f} ms"
        f" across {elapsed * 1000:.1f} ms"
    )
    print(
        f"decoded {len(received)} frames, mean abs error {mean_abs:.2f},"
        f" max jpeg queue {max_jpeg_q}, max inflight {max_inflight}"
    )
    if errors:
        for message in errors:
            print("FAIL " + message)
        return 1
    print("sim mode passed")
    return 0


def run_board():
    print("board mode reads the OV9281 on the Kria KV260, not on this laptop.")
    print("On the board:")
    print("  ./kria/build/kria_eth_camera --mode board --config config/board.conf")
    return 2


def main():
    parser = argparse.ArgumentParser(description="Get + MJPEG decode, laptop sim or Kria board")
    parser.add_argument("--mode", choices=("sim", "board"), default="sim")
    args = parser.parse_args()
    if args.mode == "sim":
        return run_sim()
    return run_board()


if __name__ == "__main__":
    sys.exit(main())
