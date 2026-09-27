using System.Net.Sockets;

namespace EthernetCameraViewer.Protocol;

internal sealed class EthernetCameraClient : IDisposable
{
    private readonly object _writeLock = new();
    private TcpClient? _client;
    private NetworkStream? _stream;
    private SynchronizationContext? _ui;
    private TaskCompletionSource<HelloInfo>? _helloTcs;
    private TaskCompletionSource<SessionEndInfo>? _stopTcs;
    private bool _closed;

    public event Action<HelloInfo>? HelloReceived;
    public event Action<VideoFrame>? FrameReceived;
    public event Action<SessionEndInfo>? SessionEnded;
    public event Action<string>? ErrorReceived;
    public event Action<string>? Disconnected;

    public async Task<HelloInfo> ConnectAsync(string host, int port, CancellationToken cancellationToken = default)
    {
        ObjectDisposedException.ThrowIf(_closed, this);
        _ui = SynchronizationContext.Current;
        _client = new TcpClient
        {
            NoDelay = true,
            ReceiveBufferSize = 8 * 1024 * 1024
        };

        using var connectTimeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        connectTimeout.CancelAfter(TimeSpan.FromSeconds(5));
        await _client.ConnectAsync(host, port, connectTimeout.Token);

        _stream = _client.GetStream();
        _helloTcs = new TaskCompletionSource<HelloInfo>(TaskCreationOptions.RunContinuationsAsynchronously);
        _ = Task.Run(ReadLoop);

        using var helloTimeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        helloTimeout.CancelAfter(TimeSpan.FromSeconds(5));
        return await _helloTcs.Task.WaitAsync(helloTimeout.Token);
    }

    public void StartSession(uint sessionId)
    {
        Send(Wire.Start, Wire.U32(sessionId));
    }

    public async Task<SessionEndInfo?> StopSessionAsync(uint sessionId)
    {
        var completion = new TaskCompletionSource<SessionEndInfo>(TaskCreationOptions.RunContinuationsAsynchronously);
        _stopTcs = completion;
        Send(Wire.Stop, Wire.U32(sessionId));
        try
        {
            return await completion.Task.WaitAsync(TimeSpan.FromSeconds(3));
        }
        catch (TimeoutException)
        {
            return null;
        }
    }

    public void Dispose()
    {
        if (_closed)
        {
            return;
        }

        _closed = true;
        try
        {
            _client?.Close();
        }
        catch (SocketException)
        {
        }

        _helloTcs?.TrySetException(new IOException("Kết nối đã đóng"));
        _stopTcs?.TrySetCanceled();
    }

    private void Send(ushort type, byte[] payload)
    {
        var stream = _stream ?? throw new InvalidOperationException("Chưa kết nối");
        var packet = Wire.Encode(type, payload);
        lock (_writeLock)
        {
            stream.Write(packet, 0, packet.Length);
            stream.Flush();
        }
    }

    private void ReadLoop()
    {
        try
        {
            var stream = _stream ?? throw new InvalidOperationException("Chưa kết nối");
            while (!_closed)
            {
                var header = Wire.ReadExact(stream, Wire.HeaderSize);
                if (!Wire.TryReadHeader(header, out var type, out var payloadLength, out var error))
                {
                    Fail(error);
                    return;
                }

                var payload = payloadLength == 0 ? Array.Empty<byte>() : Wire.ReadExact(stream, payloadLength);
                Dispatch(type, payload);
            }
        }
        catch (Exception ex) when (ex is IOException or SocketException or ObjectDisposedException or InvalidDataException)
        {
            Fail(ex.Message);
        }
    }

    private void Dispatch(ushort type, byte[] payload)
    {
        switch (type)
        {
            case Wire.Hello:
                var hello = Wire.ParseHello(payload);
                _helloTcs?.TrySetResult(hello);
                Raise(HelloReceived, hello);
                break;
            case Wire.Frame:
                // Keep the socket reader off the UI thread. Posting every frame at
                // the camera's maximum rate fills the WinForms message queue.
                FrameReceived?.Invoke(Wire.ParseFrame(payload));
                break;
            case Wire.SessionEnd:
                var end = Wire.ParseSessionEnd(payload);
                _stopTcs?.TrySetResult(end);
                Raise(SessionEnded, end);
                break;
            case Wire.Error:
                Raise(ErrorReceived, System.Text.Encoding.UTF8.GetString(payload));
                break;
        }
    }

    private void Fail(string message)
    {
        _helloTcs?.TrySetException(new IOException(message));
        _stopTcs?.TrySetException(new IOException(message));
        Raise(Disconnected, message);
    }

    private void Raise<T>(Action<T>? handler, T argument)
    {
        if (handler == null || _closed)
        {
            return;
        }

        if (_ui != null)
        {
            _ui.Post(_ => handler(argument), null);
            return;
        }

        handler(argument);
    }
}
