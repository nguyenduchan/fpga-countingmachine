# Counting machine — Kria KV260 Ethernet camera

Board **Kria KV260**, Ubuntu **24.04**, camera USB **OV9281**. Chương trình C++ trên board đọc camera và phát thành một Ethernet camera cổng TCP `5600`. Chương trình C# trên PC nhận luồng đó: **Bắt đầu** / **Dừng** là một phiên, mỗi khung hình được hiện và lưu.

Tên board và phiên bản Ubuntu nằm ở ba nơi, cùng một giá trị:

- `config/board.conf` — file thiết bị đọc lúc chạy
- `kria/CMakeLists.txt` — biên dịch vào binary
- `pc/EthernetCameraViewer/EthernetCameraViewer.csproj` — viewer trên PC

## Trên Kria KV260 (Ubuntu 24.04)

```bash
bash kria/scripts/install_deps.sh
bash kria/scripts/build.sh
./kria/build/kria_eth_camera --config config/board.conf
```

Cài dịch vụ (tự chạy khi bật máy):

```bash
bash kria/scripts/install_service.sh
```

Kiểm tra camera:

```bash
v4l2-ctl --list-devices
v4l2-ctl -d /dev/video0 --list-formats-ext
```

Nếu OV9281 không phải `/dev/video0`, sửa `video_device` trong `config/board.conf`.

Mở cổng trên board:

```bash
sudo ufw allow 5600/tcp
```

PC và Kria cùng mạng Ethernet. Ví dụ Kria `192.168.2.1`, PC `192.168.2.10`.

## Trên PC Windows

Cần .NET 8 SDK.

```powershell
dotnet run --project pc\EthernetCameraViewer\EthernetCameraViewer.csproj
```

Nhập IP của Kria, cổng `5600`, bấm **Bắt đầu**. Ảnh trực tiếp nằm ở giữa. Danh sách bên phải là từng khung hình của phiên đang chạy. **Dừng** kết thúc phiên. Bấm một dòng sau khi dừng để xem lại khung đó.

Mỗi phiên ghi JPEG vào:

`Pictures\KriaEthernetCamera\Sessions\<thời gian>_<số phiên>\`

Kèm `session.json` (tên board, Ubuntu, số khung hình).

Kiểm tra giao thức không cần camera:

```powershell
dotnet run --project pc\EthernetCameraViewer\EthernetCameraViewer.csproj -- --self-test
```

## Luồng một phiên

1. PC kết nối TCP. Board gửi HELLO: `Kria KV260`, `24.04`, `OV9281`.
2. PC gửi START với số phiên. Board mở USB camera và gửi từng khung JPEG.
3. PC gửi STOP. Board đóng camera, gửi SESSION_END, và phiên đó kết thúc.
4. Lần **Bắt đầu** sau là một phiên mới, số khung hình bắt đầu lại từ 0.
# fpga-countingmachine
