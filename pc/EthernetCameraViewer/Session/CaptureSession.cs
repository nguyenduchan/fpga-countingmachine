using System.Text.Json;
using EthernetCameraViewer.Protocol;

namespace EthernetCameraViewer.Session;

internal sealed class FrameRecord
{
    public required uint SessionId { get; init; }
    public required uint Index { get; init; }
    public required string Path { get; init; }
    public required string TimeText { get; init; }
    public required string SizeText { get; init; }
}

internal sealed class CaptureSession
{
    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };

    public uint SessionId { get; }
    public string DirectoryPath { get; }
    public string BoardName { get; }
    public string UbuntuVersion { get; }
    public string CameraModel { get; }
    public string Host { get; }
    public int Port { get; }
    public DateTime StartedLocal { get; }
    public int FrameCount { get; private set; }

    private CaptureSession(uint sessionId, string directoryPath, HelloInfo hello, string host, int port)
    {
        SessionId = sessionId;
        DirectoryPath = directoryPath;
        BoardName = hello.BoardName;
        UbuntuVersion = hello.UbuntuVersion;
        CameraModel = hello.CameraModel;
        Host = host;
        Port = port;
        StartedLocal = DateTime.Now;
    }

    public static CaptureSession Open(uint sessionId, HelloInfo hello, string host, int port)
    {
        var root = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.MyPictures),
            "KriaEthernetCamera",
            "Sessions");
        var directory = Path.Combine(root, $"{DateTime.Now:yyyyMMdd_HHmmss}_{sessionId:D4}");
        Directory.CreateDirectory(directory);
        return new CaptureSession(sessionId, directory, hello, host, port);
    }

    public FrameRecord SaveFrame(VideoFrame frame)
    {
        var path = Path.Combine(DirectoryPath, $"frame_{frame.Index:D6}.jpg");
        File.WriteAllBytes(path, frame.Jpeg);
        FrameCount++;

        var time = DateTimeOffset.FromUnixTimeMilliseconds((long)(frame.TimestampUs / 1000))
            .ToLocalTime()
            .ToString("HH:mm:ss.fff");
        return new FrameRecord
        {
            SessionId = SessionId,
            Index = frame.Index,
            Path = path,
            TimeText = time,
            SizeText = $"{frame.Width}x{frame.Height}"
        };
    }

    public void WriteSummary(SessionEndInfo? end)
    {
        var summary = new
        {
            board_name = BoardName,
            ubuntu_version = UbuntuVersion,
            camera_model = CameraModel,
            session_id = SessionId,
            host = Host,
            port = Port,
            started_local = StartedLocal.ToString("yyyy-MM-dd HH:mm:ss.fff"),
            ended_local = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff"),
            frame_count = FrameCount,
            server_frame_count = end?.FrameCount,
            duration_us = end?.DurationUs
        };
        File.WriteAllText(
            Path.Combine(DirectoryPath, "session.json"),
            JsonSerializer.Serialize(summary, JsonOptions));
    }
}
