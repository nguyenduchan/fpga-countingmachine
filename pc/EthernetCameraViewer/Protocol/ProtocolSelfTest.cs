using System.Buffers.Binary;
using System.Drawing;
using System.Drawing.Imaging;
using System.Net;
using System.Net.Sockets;
using System.Text;

namespace EthernetCameraViewer.Protocol;

internal static class ProtocolSelfTest
{
    public static void Run()
    {
        ParseHandCraftedHello();
        ParseHandCraftedFrame();
        LoopbackSession().GetAwaiter().GetResult();
    }

    private static void ParseHandCraftedHello()
    {
        var payload = new List<byte>();
        AddText(payload, "Kria KV260");
        AddText(payload, "24.04");
        AddText(payload, "OV9281");
        AddU32(payload, 1280);
        AddU32(payload, 800);
        AddU32(payload, 30);

        var hello = Wire.ParseHello(payload.ToArray());
        if (hello.BoardName != "Kria KV260" || hello.UbuntuVersion != "24.04" ||
            hello.CameraModel != "OV9281" || hello.Width != 1280 || hello.Height != 800 || hello.Fps != 30)
        {
            throw new InvalidOperationException("HELLO không khớp cấu hình board");
        }

        var packet = BuildPacket(Wire.Hello, payload.ToArray());
        if (!Wire.TryReadHeader(packet, out var type, out var length, out var error) ||
            type != Wire.Hello || length != payload.Count)
        {
            throw new InvalidOperationException(error.Length == 0 ? "Header HELLO sai" : error);
        }
    }

    private static void ParseHandCraftedFrame()
    {
        var jpeg = new byte[] { 0xFF, 0xD8, 0xFF, 0xD9 };
        var payload = new List<byte>();
        AddU32(payload, 7);
        AddU32(payload, 2);
        AddU64(payload, 1_700_000_000_000_000);
        AddU32(payload, 1280);
        AddU32(payload, 800);
        AddU32(payload, Wire.FormatJpeg);
        AddU32(payload, (uint)jpeg.Length);
        payload.AddRange(jpeg);

        var frame = Wire.ParseFrame(payload.ToArray());
        if (frame.SessionId != 7 || frame.Index != 2 || frame.Width != 1280 || frame.Height != 800 ||
            frame.Jpeg.Length != jpeg.Length)
        {
            throw new InvalidOperationException("FRAME không khớp layout");
        }
    }

    private static async Task LoopbackSession()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var port = ((IPEndPoint)listener.LocalEndpoint).Port;
        var serve = Task.Run(() => Serve(listener));

        using var client = new EthernetCameraClient();
        var frames = new List<VideoFrame>();
        SessionEndInfo? ended = null;
        client.FrameReceived += frame =>
        {
            lock (frames)
            {
                frames.Add(frame);
            }
        };
        client.SessionEnded += info => ended = info;

        var hello = await client.ConnectAsync("127.0.0.1", port);
        if (hello.BoardName != ProjectBoard.Name || hello.UbuntuVersion != ProjectBoard.UbuntuVersion)
        {
            throw new InvalidOperationException("HELLO loopback sai tên board");
        }

        client.StartSession(4);
        var result = await client.StopSessionAsync(4);
        var deadline = DateTime.UtcNow.AddSeconds(2);
        while (DateTime.UtcNow < deadline)
        {
            lock (frames)
            {
                if (frames.Count >= 3 && ended != null)
                {
                    break;
                }
            }

            await Task.Delay(10);
        }

        await serve;
        int frameCount;
        lock (frames)
        {
            frameCount = frames.Count;
        }

        if (frameCount != 3 || result == null || result.FrameCount != 3 || ended?.SessionId != 4)
        {
            throw new InvalidOperationException(
                $"Phiên loopback sai: frames={frameCount}, end={result?.FrameCount}, session={ended?.SessionId}");
        }
    }

    private static void Serve(TcpListener listener)
    {
        using var tcp = listener.AcceptTcpClient();
        listener.Stop();
        using var stream = tcp.GetStream();

        var hello = new List<byte>();
        AddText(hello, ProjectBoard.Name);
        AddText(hello, ProjectBoard.UbuntuVersion);
        AddText(hello, ProjectBoard.CameraModel);
        AddU32(hello, 1280);
        AddU32(hello, 800);
        AddU32(hello, 30);
        var helloPacket = BuildPacket(Wire.Hello, hello.ToArray());
        stream.Write(helloPacket);

        var start = ReadMessage(stream);
        if (start.Type != Wire.Start || Wire.ParseSessionId(start.Payload) != 4)
        {
            throw new InvalidOperationException("Không nhận được START phiên 4");
        }

        var jpeg = SampleJpeg();
        for (uint index = 0; index < 3; index++)
        {
            var payload = new List<byte>();
            AddU32(payload, 4);
            AddU32(payload, index);
            AddU64(payload, (ulong)DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() * 1000);
            AddU32(payload, 8);
            AddU32(payload, 8);
            AddU32(payload, Wire.FormatJpeg);
            AddU32(payload, (uint)jpeg.Length);
            payload.AddRange(jpeg);
            var packet = BuildPacket(Wire.Frame, payload.ToArray());
            stream.Write(packet);
        }

        var stop = ReadMessage(stream);
        if (stop.Type != Wire.Stop || Wire.ParseSessionId(stop.Payload) != 4)
        {
            throw new InvalidOperationException("Không nhận được STOP phiên 4");
        }

        var end = new List<byte>();
        AddU32(end, 4);
        AddU32(end, 3);
        AddU64(end, 1000);
        var endPacket = BuildPacket(Wire.SessionEnd, end.ToArray());
        stream.Write(endPacket);
    }

    private static (ushort Type, byte[] Payload) ReadMessage(NetworkStream stream)
    {
        var header = Wire.ReadExact(stream, Wire.HeaderSize);
        if (!Wire.TryReadHeader(header, out var type, out var length, out var error))
        {
            throw new InvalidOperationException(error);
        }

        var payload = length == 0 ? Array.Empty<byte>() : Wire.ReadExact(stream, length);
        return (type, payload);
    }

    private static byte[] BuildPacket(ushort type, byte[] payload)
    {
        var packet = new byte[Wire.HeaderSize + payload.Length];
        packet[0] = (byte)'E';
        packet[1] = (byte)'T';
        packet[2] = (byte)'H';
        packet[3] = (byte)'C';
        packet[4] = (byte)(Wire.Version & 0xff);
        packet[5] = (byte)((Wire.Version >> 8) & 0xff);
        packet[6] = (byte)(type & 0xff);
        packet[7] = (byte)((type >> 8) & 0xff);
        BinaryPrimitives.WriteUInt32LittleEndian(packet.AsSpan(8), (uint)payload.Length);
        payload.CopyTo(packet.AsSpan(Wire.HeaderSize));
        return packet;
    }

    private static byte[] SampleJpeg()
    {
        using var bitmap = new Bitmap(8, 8);
        using (var graphics = Graphics.FromImage(bitmap))
        {
            graphics.Clear(Color.DimGray);
        }

        using var stream = new MemoryStream();
        bitmap.Save(stream, ImageFormat.Jpeg);
        return stream.ToArray();
    }

    private static void AddText(List<byte> buffer, string text)
    {
        var bytes = Encoding.UTF8.GetBytes(text);
        buffer.Add((byte)(bytes.Length & 0xff));
        buffer.Add((byte)((bytes.Length >> 8) & 0xff));
        buffer.AddRange(bytes);
    }

    private static void AddU32(List<byte> buffer, uint value)
    {
        buffer.Add((byte)(value & 0xff));
        buffer.Add((byte)((value >> 8) & 0xff));
        buffer.Add((byte)((value >> 16) & 0xff));
        buffer.Add((byte)((value >> 24) & 0xff));
    }

    private static void AddU64(List<byte> buffer, ulong value)
    {
        for (var shift = 0; shift < 64; shift += 8)
        {
            buffer.Add((byte)((value >> shift) & 0xff));
        }
    }
}
