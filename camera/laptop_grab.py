# Hands the laptop webcam to eth_camera as grayscale bytes.
# The C++ program does the shading. This process only reads the camera.
import socket
import struct

import cv2

cap = cv2.VideoCapture(0, cv2.CAP_DSHOW)
cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
if not cap.isOpened():
    raise SystemExit("Cannot open the laptop camera")

sock = socket.create_connection(("127.0.0.1", 5601))
print("sending camera frames to 127.0.0.1:5601")
try:
    while True:
        ok, frame = cap.read()
        if not ok:
            continue
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        height, width = gray.shape
        sock.sendall(struct.pack("<II", width, height) + gray.tobytes())
except (BrokenPipeError, ConnectionError):
    pass
finally:
    sock.close()
    cap.release()
