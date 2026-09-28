using System.Buffers;
using System.Diagnostics;
using System.Text.Json;
using EthernetCameraViewer.Protocol;
using EthernetCameraViewer.Session;

namespace EthernetCameraViewer;

internal sealed class MainForm : Form
{
    private static readonly Color StartColor = Color.FromArgb(27, 94, 32);
    private static readonly Color StopColor = Color.FromArgb(183, 28, 28);
    private static readonly Color FolderColor = Color.FromArgb(55, 71, 79);
    private static readonly Color DisabledColor = Color.FromArgb(189, 189, 189);

    private readonly TextBox _host = new() { Width = 200, Text = "127.0.0.1" };
    private readonly TextBox _port = new() { Width = 70, Text = "5600" };
    private readonly Button _start = new() { Text = "Bắt đầu", Width = 110, Height = 34 };
    private readonly Button _stop = new() { Text = "Dừng", Width = 110, Height = 34 };
    private readonly Button _openFolder = new() { Text = "Mở thư mục phiên", Width = 160, Height = 34 };
    private readonly Label _boardLabel = new() { AutoEllipsis = true, Height = 24 };
    private readonly Label _overlay = new()
    {
        Dock = DockStyle.Top,
        Height = 28,
        ForeColor = Color.White,
        BackColor = Color.FromArgb(32, 32, 32),
        TextAlign = ContentAlignment.MiddleLeft,
        Padding = new Padding(8, 0, 0, 0),
        Text = "Chưa có khung hình"
    };
    private readonly PictureBox _picture = new()
    {
        Dock = DockStyle.Fill,
        BackColor = Color.Black,
        SizeMode = PictureBoxSizeMode.Zoom
    };
    private readonly ListView _framesView = new()
    {
        Dock = DockStyle.Fill,
        View = View.Details,
        VirtualMode = true,
        FullRowSelect = true,
        GridLines = true,
        MultiSelect = false,
        HeaderStyle = ColumnHeaderStyle.Nonclickable
    };
    private readonly Label _status = new()
    {
        Dock = DockStyle.Fill,
        TextAlign = ContentAlignment.MiddleLeft,
        AutoEllipsis = true,
        Padding = new Padding(8, 0, 0, 0)
    };

    private readonly List<FrameRecord> _frames = new();
    private readonly object _saveLock = new();
    private readonly Queue<(CaptureSession Session, VideoFrame Frame)> _saveQueue = new();
    private readonly System.Collections.Concurrent.ConcurrentQueue<FrameRecord> _savedFrames = new();
    private readonly object _latestLock = new();
    private readonly System.Windows.Forms.Timer _uiTimer;
    private VideoFrame? _latestFrame;
    private int _receivedFrames;
    private int _fpsSampleCount;
    private DateTime _fpsSampleTime = DateTime.UtcNow;
    private double _receiveFps;
    private int _saveRunning;
    private readonly string _settingsPath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "KriaEthernetCamera",
        "viewer-settings.json");

    private EthernetCameraClient? _client;
    private CaptureSession? _session;
    private HelloInfo? _helloInfo;
    private bool _connected;
    private string _connectedHost = "";
    private int _connectedPort;
    private uint _nextSessionId = 1;
    private uint _sessionId;
    private bool _streaming;
    private bool _sessionOpen;
    private bool _busy;
    private bool _closing;
    private string? _lastError;
    private readonly bool _localhost;

    public MainForm(string[] args)
    {
        Text = $"Ethernet Camera Viewer — {ProjectBoard.Name} / Ubuntu {ProjectBoard.UbuntuVersion}";
        Font = new Font("Segoe UI", 10F);
        ClientSize = new Size(1200, 760);
        MinimumSize = new Size(960, 640);
        StartPosition = FormStartPosition.CenterScreen;
        KeyPreview = true;
        DoubleBuffered = true;

        BuildLayout();
        _localhost = args.Any(arg => arg == "--localhost");
        LoadSettings(args.Where(arg => arg != "--localhost").ToArray());
        if (_localhost)
        {
            _host.Text = "127.0.0.1";
            _port.Text = "5600";
            Shown += (_, _) => OnStartClick(this, EventArgs.Empty);
        }
        SetButton(_start, false, StartColor);
        SetButton(_stop, false, StopColor);
        SetButton(_openFolder, false, FolderColor);
        _start.Enabled = true;
        SetButton(_start, true, StartColor);
        _boardLabel.Text = $"Mục tiêu: {ProjectBoard.Name}, Ubuntu {ProjectBoard.UbuntuVersion}, camera {ProjectBoard.CameraModel}";
        _status.Text = "C# chỉ đọc luồng và hiển thị. Bấm Bắt đầu để nối localhost:5600.";

        _start.Click += OnStartClick;
        _stop.Click += OnStopClick;
        _openFolder.Click += OnOpenFolderClick;
        _framesView.RetrieveVirtualItem += OnRetrieveFrame;
        _framesView.SelectedIndexChanged += OnFrameSelected;
        KeyDown += OnFormKeyDown;
        _uiTimer = new System.Windows.Forms.Timer { Interval = 15 };
        _uiTimer.Tick += OnUiTick;
        _uiTimer.Start();
    }

    private void BuildLayout()
    {
        var root = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 1,
            RowCount = 3,
            Padding = new Padding(0)
        };
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 92));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 32));

        var toolbar = new Panel { Dock = DockStyle.Fill, BackColor = Color.White };
        AddField(toolbar, "Địa chỉ Kria", 12, _host, 118);
        AddField(toolbar, "Cổng", 332, _port, 376);
        _start.Location = new Point(460, 10);
        _stop.Location = new Point(580, 10);
        _openFolder.Location = new Point(700, 10);
        _boardLabel.Location = new Point(12, 54);
        toolbar.Controls.Add(_start);
        toolbar.Controls.Add(_stop);
        toolbar.Controls.Add(_openFolder);
        toolbar.Controls.Add(_boardLabel);
        toolbar.Resize += (_, _) =>
        {
            _boardLabel.Width = Math.Max(200, toolbar.ClientSize.Width - 24);
        };

        var split = new SplitContainer
        {
            Dock = DockStyle.Fill,
            Orientation = Orientation.Vertical,
            FixedPanel = FixedPanel.Panel2,
            SplitterWidth = 6
        };
        var videoHost = new Panel { Dock = DockStyle.Fill, BackColor = Color.Black };
        videoHost.Controls.Add(_picture);
        videoHost.Controls.Add(_overlay);
        split.Panel1.Controls.Add(videoHost);

        _framesView.Columns.Add("Khung", 80);
        _framesView.Columns.Add("Thời điểm", 130);
        _framesView.Columns.Add("Kích thước", 100);
        split.Panel2.Controls.Add(_framesView);
        Shown += (_, _) =>
        {
            try
            {
                split.Panel1MinSize = 240;
                split.Panel2MinSize = 280;
                var distance = split.Width - 340;
                if (distance > split.Panel1MinSize && distance < split.Width - split.Panel2MinSize)
                {
                    split.SplitterDistance = distance;
                }
            }
            catch (InvalidOperationException)
            {
                // The splitter keeps its default position when the window is still too narrow.
            }
        };

        var statusBar = new Panel { Dock = DockStyle.Fill, BackColor = Color.FromArgb(245, 245, 245) };
        statusBar.Controls.Add(_status);

        root.Controls.Add(toolbar, 0, 0);
        root.Controls.Add(split, 0, 1);
        root.Controls.Add(statusBar, 0, 2);
        Controls.Add(root);
    }

    private static void AddField(Control parent, string caption, int labelX, Control editor, int editorX)
    {
        var label = new Label
        {
            Text = caption,
            AutoSize = true,
            Location = new Point(labelX, 16)
        };
        editor.Location = new Point(editorX, 12);
        parent.Controls.Add(label);
        parent.Controls.Add(editor);
    }

    private void LoadSettings(string[] args)
    {
        var configPath = FindProjectConfig(args);
        var plainArgs = args.Where((arg, index) => arg != "--config" && (index == 0 || args[index - 1] != "--config")).ToArray();
        args = plainArgs;
        try
        {
            if (File.Exists(_settingsPath))
            {
                var settings = JsonSerializer.Deserialize<ViewerSettings>(File.ReadAllText(_settingsPath));
                if (settings != null)
                {
                    if (!string.IsNullOrWhiteSpace(settings.Host))
                    {
                        _host.Text = settings.Host;
                    }

                    if (settings.Port is > 0 and <= 65535)
                    {
                        _port.Text = settings.Port.ToString();
                    }
                }
            }
        }
        catch (Exception ex) when (ex is IOException or JsonException or UnauthorizedAccessException)
        {
            _status.Text = "Không đọc được cài đặt cũ: " + ex.Message;
        }

        ApplyProjectConfig(configPath);

        if (args.Length >= 1)
        {
            _host.Text = args[0];
        }

        if (args.Length >= 2 && int.TryParse(args[1], out var port) && port is > 0 and <= 65535)
        {
            _port.Text = port.ToString();
        }
    }

    private static string? FindProjectConfig(string[] args)
    {
        for (var index = 0; index < args.Length - 1; ++index)
        {
            if (args[index] == "--config")
            {
                return args[index + 1];
            }
        }

        foreach (var candidate in new[] { "config/laptop.conf", "../config/laptop.conf", "../../config/laptop.conf" })
        {
            if (File.Exists(candidate))
            {
                return candidate;
            }
        }

        return null;
    }

    private void ApplyProjectConfig(string? path)
    {
        if (path == null || !File.Exists(path))
        {
            return;
        }

        string? host = null;
        string? bindAddress = null;
        var port = 0;
        foreach (var raw in File.ReadAllLines(path))
        {
            var line = raw.Trim();
            if (line.Length == 0 || line[0] == '#')
            {
                continue;
            }

            var split = line.IndexOf('=');
            if (split <= 0)
            {
                continue;
            }

            var key = line[..split].Trim();
            var value = line[(split + 1)..].Trim();
            if (key == "host")
            {
                host = value;
            }
            else if (key == "bind_address")
            {
                bindAddress = value;
            }
            else if (key == "port" && int.TryParse(value, out var parsed))
            {
                port = parsed;
            }
        }

        if (string.IsNullOrWhiteSpace(host) && !string.IsNullOrWhiteSpace(bindAddress) && bindAddress != "0.0.0.0")
        {
            host = bindAddress;
        }

        if (!string.IsNullOrWhiteSpace(host))
        {
            _host.Text = host;
        }

        if (port is > 0 and <= 65535)
        {
            _port.Text = port.ToString();
        }
    }

    private void SaveSettings()
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(_settingsPath)!);
            var settings = new ViewerSettings { Host = _host.Text.Trim(), Port = int.Parse(_port.Text.Trim()) };
            File.WriteAllText(_settingsPath, JsonSerializer.Serialize(settings));
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or FormatException)
        {
            SetStatus("Không lưu được địa chỉ: " + ex.Message);
        }
    }

    private async void OnStartClick(object? sender, EventArgs e)
    {
        if (_busy || _streaming)
        {
            return;
        }

        _busy = true;
        SetButton(_start, false, StartColor);
        try
        {
            var host = _host.Text.Trim();
            if (host.Length == 0)
            {
                MessageBox.Show(this, "Nhập địa chỉ IP của board Kria trên cổng Ethernet.", "Thiếu địa chỉ",
                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
                SetButton(_start, true, StartColor);
                return;
            }

            if (!int.TryParse(_port.Text.Trim(), out var port) || port is < 1 or > 65535)
            {
                MessageBox.Show(this, "Cổng phải từ 1 đến 65535.", "Cổng không hợp lệ",
                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
                SetButton(_start, true, StartColor);
                return;
            }

            _lastError = null;
            _host.Enabled = false;
            _port.Enabled = false;
            var hello = await EnsureConnected(host, port);
            if (_closing)
            {
                return;
            }

            ApplyHello(hello);
            SaveSettings();
            var sessionId = _nextSessionId++;
            if (_nextSessionId == 0)
            {
                _nextSessionId = 1;
            }

            BeginSession(hello, sessionId, host, port);
            _streaming = true;
            _sessionOpen = true;
            _client!.StartSession(sessionId);
            SetButton(_stop, true, StopColor);
            SetButton(_openFolder, true, FolderColor);
            _host.Enabled = false;
            _port.Enabled = false;
            SetStatus($"Phiên {sessionId} đang chạy. Bấm Dừng để kết thúc phiên.");
        }
        catch (Exception ex)
        {
            FailStart(ex.Message);
        }
        finally
        {
            _busy = false;
        }
    }

    private async void OnStopClick(object? sender, EventArgs e)
    {
        if (_busy || !_streaming || _client == null)
        {
            return;
        }

        _busy = true;
        SetButton(_stop, false, StopColor);
        SetStatus($"Đang dừng phiên {_sessionId}...");
        try
        {
            var end = await _client.StopSessionAsync(_sessionId);
            if (end == null)
            {
                DropConnection();
            }

            if (!_closing)
            {
                FinishSession(end);
            }
        }
        catch (Exception ex)
        {
            if (!_closing)
            {
                FinishSession(null);
                SetStatus("Dừng phiên: " + ex.Message);
            }
        }
        finally
        {
            _busy = false;
        }
    }

    private void OnOpenFolderClick(object? sender, EventArgs e)
    {
        if (_session == null || !Directory.Exists(_session.DirectoryPath))
        {
            return;
        }

        Process.Start(new ProcessStartInfo
        {
            FileName = _session.DirectoryPath,
            UseShellExecute = true
        });
    }

    private void OnFormKeyDown(object? sender, KeyEventArgs e)
    {
        if (e.KeyCode == Keys.Escape && _streaming)
        {
            OnStopClick(this, EventArgs.Empty);
            e.Handled = true;
        }
    }

    private async Task<HelloInfo> EnsureConnected(string host, int port)
    {
        if (_connected && _client != null && _helloInfo != null &&
            string.Equals(_connectedHost, host, StringComparison.OrdinalIgnoreCase) &&
            _connectedPort == port)
        {
            SetStatus($"Đang mở phiên trên {host}:{port}...");
            return _helloInfo;
        }

        SetStatus($"Đang kết nối {host}:{port}...");
        ReplaceClient();
        var hello = await _client!.ConnectAsync(host, port);
        _connected = true;
        _helloInfo = hello;
        _connectedHost = host;
        _connectedPort = port;
        return hello;
    }

    private void DropConnection()
    {
        _connected = false;
        _helloInfo = null;
        if (_client == null)
        {
            return;
        }

        _client.HelloReceived -= OnHello;
        _client.FrameReceived -= OnFrame;
        _client.SessionEnded -= OnSessionEnded;
        _client.ErrorReceived -= OnError;
        _client.Disconnected -= OnDisconnected;
        _client.Dispose();
        _client = null;
    }

    private void ReplaceClient()
    {
        if (_client != null)
        {
            _client.HelloReceived -= OnHello;
            _client.FrameReceived -= OnFrame;
            _client.SessionEnded -= OnSessionEnded;
            _client.ErrorReceived -= OnError;
            _client.Disconnected -= OnDisconnected;
            _client.Dispose();
        }

        _client = new EthernetCameraClient();
        _client.HelloReceived += OnHello;
        _client.FrameReceived += OnFrame;
        _client.SessionEnded += OnSessionEnded;
        _client.ErrorReceived += OnError;
        _client.Disconnected += OnDisconnected;
    }

    private void BeginSession(HelloInfo hello, uint sessionId, string host, int port)
    {
        _sessionId = sessionId;
        _session = CaptureSession.Open(sessionId, hello, host, port);
        while (_savedFrames.TryDequeue(out _))
        {
        }

        _frames.Clear();
        _receivedFrames = 0;
        _fpsSampleCount = 0;
        _fpsSampleTime = DateTime.UtcNow;
        _receiveFps = 0;
        lock (_latestLock)
        {
            _latestFrame = null;
        }

        _framesView.VirtualListSize = 0;
        ReplaceImage(null);
        _overlay.Text = $"Phiên {sessionId} — đang mở camera {hello.CameraModel}";
    }

    private void ApplyHello(HelloInfo hello)
    {
        _boardLabel.Text =
            $"Board: {hello.BoardName}    Ubuntu: {hello.UbuntuVersion}    Camera: {hello.CameraModel}    {hello.Width}x{hello.Height} @ {hello.Fps} fps";
        Text = $"Ethernet Camera Viewer — {hello.BoardName} / Ubuntu {hello.UbuntuVersion}";
    }

    private void OnHello(HelloInfo hello)
    {
        if (_closing || IsDisposed)
        {
            return;
        }

        ApplyHello(hello);
    }

    private void OnFrame(VideoFrame frame)
    {
        var session = _session;
        if (_closing || session == null || frame.SessionId != session.SessionId)
        {
            return;
        }

        EnqueueSave(session, frame);
        Interlocked.Increment(ref _receivedFrames);
        lock (_latestLock)
        {
            _latestFrame = frame;
        }
    }

    private void OnUiTick(object? sender, EventArgs e)
    {
        VideoFrame? frame;
        lock (_latestLock)
        {
            frame = _latestFrame;
            _latestFrame = null;
        }

        if (frame != null)
        {
            ShowJpeg(frame.Jpeg);
        }

        var added = 0;
        while (added < 400 && _savedFrames.TryDequeue(out var saved))
        {
            if (saved.SessionId != _sessionId)
            {
                continue;
            }

            _frames.Add(saved);
            added++;
        }

        if (added > 0)
        {
            _framesView.VirtualListSize = _frames.Count;
            if (_streaming && _frames.Count > 0)
            {
                _framesView.EnsureVisible(_frames.Count - 1);
            }
        }

        if (!_streaming && frame == null && added == 0)
        {
            return;
        }

        var received = Volatile.Read(ref _receivedFrames);
        var now = DateTime.UtcNow;
        var elapsed = (now - _fpsSampleTime).TotalSeconds;
        if (elapsed >= 0.5)
        {
            _receiveFps = (received - _fpsSampleCount) / elapsed;
            _fpsSampleCount = received;
            _fpsSampleTime = now;
        }

        if (_session == null)
        {
            return;
        }

        var shown = frame == null ? "" : $"    {frame.Width}x{frame.Height}    khung {frame.Index}";
        _overlay.Text = $"Phiên {_sessionId}{shown}    {_receiveFps:0.0} fps";
        if (_streaming)
        {
            SetStatus($"Phiên {_sessionId}: {received} khung đã nhận, {_receiveFps:0.0} fps. {_session.DirectoryPath}");
        }
    }

    private void EnqueueSave(CaptureSession session, VideoFrame frame)
    {
        lock (_saveLock)
        {
            _saveQueue.Enqueue((session, frame));
        }

        if (Interlocked.CompareExchange(ref _saveRunning, 1, 0) == 0)
        {
            ThreadPool.QueueUserWorkItem(_ => DrainSaves());
        }
    }

    private void DrainSaves()
    {
        while (true)
        {
            (CaptureSession Session, VideoFrame Frame) job;
            lock (_saveLock)
            {
                if (_saveQueue.Count == 0)
                {
                    break;
                }

                job = _saveQueue.Dequeue();
            }

            FrameRecord? record = null;
            try
            {
                record = job.Session.SaveFrame(job.Frame);
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            {
                if (!IsDisposed)
                {
                    BeginInvoke(() => SetStatus("Không ghi được khung hình: " + ex.Message));
                }
            }

            if (record == null || IsDisposed || _closing)
            {
                continue;
            }

            _savedFrames.Enqueue(record);
        }

        Volatile.Write(ref _saveRunning, 0);
        lock (_saveLock)
        {
            if (_saveQueue.Count > 0 && Interlocked.CompareExchange(ref _saveRunning, 1, 0) == 0)
            {
                ThreadPool.QueueUserWorkItem(_ => DrainSaves());
            }
        }
    }

    private void OnSessionEnded(SessionEndInfo end)
    {
        if (_closing || IsDisposed || end.SessionId != _sessionId)
        {
            return;
        }

        FinishSession(end);
    }

    private void OnError(string message)
    {
        if (_closing || IsDisposed)
        {
            return;
        }

        _lastError = message;
        SetStatus(message);
    }

    private void OnDisconnected(string reason)
    {
        if (_closing || IsDisposed)
        {
            return;
        }

        _connected = false;
        if (_sessionOpen)
        {
            FinishSession(null);
            SetStatus("Mất kết nối Ethernet: " + reason);
            return;
        }

        if (!_busy)
        {
            SetStatus("Mất kết nối Ethernet: " + reason);
        }
    }

    private void FinishSession(SessionEndInfo? end)
    {
        if (!_sessionOpen && !_streaming)
        {
            return;
        }

        _streaming = false;
        _sessionOpen = false;
        try
        {
            _session?.WriteSummary(end);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            _lastError = ex.Message;
        }

        SetButton(_start, true, StartColor);
        SetButton(_stop, false, StopColor);
        _host.Enabled = true;
        _port.Enabled = true;
        var count = _frames.Count;
        var duration = end == null ? "" : $", {(end.DurationUs / 1_000_000.0):0.0} giây";
        var error = string.IsNullOrEmpty(_lastError) ? "" : " " + _lastError;
        SetStatus($"Phiên {_sessionId} đã kết thúc: {count} khung hình{duration}.{error} {_session?.DirectoryPath}");
        _overlay.Text = $"Phiên {_sessionId} đã kết thúc — {count} khung hình. Chọn một dòng để xem lại.";
    }

    private void FailStart(string message)
    {
        var began = _sessionOpen || _streaming;
        _streaming = false;
        _sessionOpen = false;
        if (began)
        {
            try
            {
                _session?.WriteSummary(null);
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            {
                _lastError = ex.Message;
            }
        }

        DropConnection();
        SetButton(_start, true, StartColor);
        SetButton(_stop, false, StopColor);
        _host.Enabled = true;
        _port.Enabled = true;
        SetStatus(message);
        MessageBox.Show(this, message, "Không bắt đầu được phiên", MessageBoxButtons.OK, MessageBoxIcon.Warning);
    }

    private void OnRetrieveFrame(object? sender, RetrieveVirtualItemEventArgs e)
    {
        if (e.ItemIndex < 0 || e.ItemIndex >= _frames.Count)
        {
            e.Item = new ListViewItem(string.Empty);
            return;
        }

        var frame = _frames[e.ItemIndex];
        var item = new ListViewItem(frame.Index.ToString());
        item.SubItems.Add(frame.TimeText);
        item.SubItems.Add(frame.SizeText);
        e.Item = item;
    }

    private void OnFrameSelected(object? sender, EventArgs e)
    {
        if (_streaming || _framesView.SelectedIndices.Count == 0)
        {
            return;
        }

        var index = _framesView.SelectedIndices[0];
        if (index < 0 || index >= _frames.Count)
        {
            return;
        }

        try
        {
            ShowJpeg(File.ReadAllBytes(_frames[index].Path));
            _overlay.Text = $"Xem lại khung {_frames[index].Index}    {_frames[index].TimeText}    {_frames[index].SizeText}";
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            SetStatus(ex.Message);
        }
    }

    private void ShowJpeg(byte[] jpeg)
    {
        try
        {
            if (jpeg.Length > 8 && jpeg[0] == (byte)'S' && jpeg[1] == (byte)'T' && jpeg[2] == (byte)'0' && jpeg[3] == (byte)'1')
            {
                ReplaceImage(ComposeStrips(jpeg));
                return;
            }
            using var stream = new MemoryStream(jpeg);
            using var image = Image.FromStream(stream, false, true);
            ReplaceImage(new Bitmap(image));
        }
        catch (ArgumentException)
        {
            SetStatus("Khung hình JPEG không đọc được");
        }
    }

    private static uint ReadU32(byte[] data, ref int offset)
    {
        var value = (uint)(data[offset] | (data[offset + 1] << 8) | (data[offset + 2] << 16) | (data[offset + 3] << 24));
        offset += 4;
        return value;
    }

    private static Bitmap ComposeStrips(byte[] packed)
    {
        var offset = 4;
        var count = ReadU32(packed, ref offset);
        var strips = new List<(int Height, byte[] Jpeg)>();
        var totalHeight = 0;
        var width = 0;
        for (var band = 0; band < count; ++band)
        {
            var height = (int)ReadU32(packed, ref offset);
            var length = (int)ReadU32(packed, ref offset);
            var jpeg = new byte[length];
            Buffer.BlockCopy(packed, offset, jpeg, 0, length);
            offset += length;
            using var stream = new MemoryStream(jpeg);
            using var image = Image.FromStream(stream, false, true);
            width = image.Width;
            totalHeight += height;
            strips.Add((height, jpeg));
        }
        var bitmap = new Bitmap(width, totalHeight);
        using var graphics = Graphics.FromImage(bitmap);
        var y = 0;
        foreach (var strip in strips)
        {
            using var stream = new MemoryStream(strip.Jpeg);
            using var image = Image.FromStream(stream, false, true);
            graphics.DrawImage(image, 0, y, image.Width, strip.Height);
            y += strip.Height;
        }
        return bitmap;
    }

    private void ReplaceImage(Image? image)
    {
        var previous = _picture.Image;
        _picture.Image = image;
        previous?.Dispose();
    }

    private void SetStatus(string text)
    {
        _status.Text = text;
    }

    private static void SetButton(Button button, bool enabled, Color active)
    {
        button.Enabled = enabled;
        button.FlatStyle = FlatStyle.Flat;
        button.FlatAppearance.BorderSize = 0;
        button.Cursor = enabled ? Cursors.Hand : Cursors.Default;
        button.BackColor = enabled ? active : DisabledColor;
        button.ForeColor = enabled ? Color.White : Color.FromArgb(80, 80, 80);
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        _closing = true;
        _uiTimer.Stop();
        _client?.Dispose();
        _picture.Image?.Dispose();
        base.OnFormClosing(e);
    }
}

internal sealed class ViewerSettings
{
    public string Host { get; set; } = "";
    public int Port { get; set; } = 5600;
}
