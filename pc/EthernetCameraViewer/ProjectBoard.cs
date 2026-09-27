using System.Reflection;

namespace EthernetCameraViewer;

internal static class ProjectBoard
{
    public static string Name { get; } = Read("BoardName", "Kria KV260");
    public static string UbuntuVersion { get; } = Read("UbuntuVersion", "24.04");
    public static string CameraModel { get; } = Read("CameraModel", "OV9281");

    private static string Read(string key, string fallback)
    {
        var metadata = Assembly.GetExecutingAssembly()
            .GetCustomAttributes<AssemblyMetadataAttribute>()
            .FirstOrDefault(item => item.Key == key);
        return string.IsNullOrWhiteSpace(metadata?.Value) ? fallback : metadata.Value;
    }
}
