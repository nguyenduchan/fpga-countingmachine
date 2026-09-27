using System.Buffers.Binary;
using System.Net.Sockets;
using System.Text;

namespace EthernetCameraViewer.Protocol;

internal static class Wire
{
    public const ushort Version = 1;
    public const int HeaderSize = 12;
    public const uint MaxPayload = 8u * 1024u * 1024u;

    public const ushort Hello = 1;
    public const ushort Start = 2;
    public const ushort Stop = 3;
    public const ushort Frame = 4;
    public const ushort SessionEnd = 5;
    public const ushort Error = 6;
    public const uint FormatJpeg = 1;
    public const uint FormatStrips = 2;

    public static byte[] Encode(ushort type, byte[] payload)
    {
        var packet = new byte[HeaderSize + payload.Length];
        packet[0] = (byte)'E';
        packet[1] = (byte)'T';
        packet[2] = (byte)'H';
        packet[3] = (byte)'C';
        BinaryPrimitives.WriteUInt16LittleEndian(packet.AsSpan(4), Version);
        BinaryPrimitives.WriteUInt16LittleEndian(packet.AsSpan(6), type);
        BinaryPrimitives.WriteUInt32LittleEndian(packet.AsSpan(8), (uint)payload.Length);
        payload.CopyTo(packet.AsSpan(HeaderSize));
        return packet;
    }

    public static byte[] U32(uint value)
    {
        var bytes = new byte[4];
        BinaryPrimitives.WriteUInt32LittleEndian(bytes, value);
        return bytes;
    }

    public static bool TryReadHeader(ReadOnlySpan<byte> header, out ushort type, out int payloadLength, out string error)
    {
        type = 0;
        payloadLength = 0;
        error = "";
        if (header.Length < HeaderSize ||
            header[0] != (byte)'E' || header[1] != (byte)'T' ||
            header[2] != (byte)'H' || header[3] != (byte)'C')
        {
            error = "Sai định dạng gói tin Ethernet camera";
            return false;
        }

        var version = BinaryPrimitives.ReadUInt16LittleEndian(header.Slice(4));
        if (version != Version)
        {
            error = "Phiên bản giao thức không khớp";
            return false;
        }

        type = BinaryPrimitives.ReadUInt16LittleEndian(header.Slice(6));
        var length = BinaryPrimitives.ReadUInt32LittleEndian(header.Slice(8));
        if (length > MaxPayload)
        {
            error = "Khung hình vượt quá giới hạn";
            return false;
        }

        payloadLength = (int)length;
        return true;
    }

    public static HelloInfo ParseHello(byte[] payload)
    {
        var cursor = new Cursor(payload);
        var board = cursor.Text();
        var ubuntu = cursor.Text();
        var camera = cursor.Text();
        var width = cursor.U32();
        var height = cursor.U32();
        var fps = cursor.U32();
        return new HelloInfo(board, ubuntu, camera, width, height, fps);
    }

    public static uint ParseSessionId(byte[] payload)
    {
        var cursor = new Cursor(payload);
        return cursor.U32();
    }

    public static VideoFrame ParseFrame(byte[] payload)
    {
        var cursor = new Cursor(payload);
        var sessionId = cursor.U32();
        var index = cursor.U32();
        var timestampUs = cursor.U64();
        var width = cursor.U32();
        var height = cursor.U32();
        var format = cursor.U32();
        var dataLength = cursor.U32();
        if (format != FormatJpeg && format != FormatStrips)
        {
            throw new InvalidDataException("Khung hình không phải JPEG");
        }

        var jpeg = cursor.Bytes(checked((int)dataLength));
        if (cursor.Remaining != 0)
        {
            throw new InvalidDataException("Độ dài JPEG không khớp");
        }

        return new VideoFrame(sessionId, index, timestampUs, width, height, jpeg);
    }

    public static SessionEndInfo ParseSessionEnd(byte[] payload)
    {
        var cursor = new Cursor(payload);
        return new SessionEndInfo(cursor.U32(), cursor.U32(), cursor.U64());
    }

    public static byte[] ReadExact(NetworkStream stream, int count)
    {
        var buffer = new byte[count];
        var offset = 0;
        while (offset < count)
        {
            var read = stream.Read(buffer, offset, count - offset);
            if (read == 0)
            {
                throw new IOException("Kết nối đã đóng");
            }

            offset += read;
        }

        return buffer;
    }

    private sealed class Cursor
    {
        private readonly byte[] _data;
        private int _offset;

        public Cursor(byte[] data)
        {
            _data = data;
        }

        public int Remaining => _data.Length - _offset;

        public ushort U16()
        {
            Ensure(2);
            var value = BinaryPrimitives.ReadUInt16LittleEndian(_data.AsSpan(_offset));
            _offset += 2;
            return value;
        }

        public uint U32()
        {
            Ensure(4);
            var value = BinaryPrimitives.ReadUInt32LittleEndian(_data.AsSpan(_offset));
            _offset += 4;
            return value;
        }

        public ulong U64()
        {
            Ensure(8);
            var value = BinaryPrimitives.ReadUInt64LittleEndian(_data.AsSpan(_offset));
            _offset += 8;
            return value;
        }

        public string Text()
        {
            var length = U16();
            Ensure(length);
            var text = Encoding.UTF8.GetString(_data, _offset, length);
            _offset += length;
            return text;
        }

        public byte[] Bytes(int length)
        {
            Ensure(length);
            var slice = new byte[length];
            Buffer.BlockCopy(_data, _offset, slice, 0, length);
            _offset += length;
            return slice;
        }

        private void Ensure(int length)
        {
            if (length < 0 || _offset + length > _data.Length)
            {
                throw new InvalidDataException("Gói tin bị cắt");
            }
        }
    }
}

internal sealed record HelloInfo(string BoardName, string UbuntuVersion, string CameraModel, uint Width, uint Height, uint Fps);

internal sealed record VideoFrame(uint SessionId, uint Index, ulong TimestampUs, uint Width, uint Height, byte[] Jpeg);

internal sealed record SessionEndInfo(uint SessionId, uint FrameCount, ulong DurationUs);
