using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using Windows.Graphics;
using Windows.Graphics.Capture;
using Windows.Graphics.DirectX;
using Windows.Graphics.DirectX.Direct3D11;
using Windows.Graphics.Imaging;
using Windows.Storage.Streams;

namespace TTSL.Services;

internal sealed class WindowsGraphicsWindowCapture : IDisposable
{
    private static readonly Guid IGraphicsCaptureItemGuid = new("79C3F95B-31F7-4EC2-A464-632EF5D30760");
    private static readonly Guid IDXGIDeviceGuid = new("54EC77FA-1377-44E6-8C32-88FD5F44C84C");

    private readonly object sync = new();
    private readonly AutoResetEvent frameArrived = new(false);

    private IDirect3DDevice? device;
    private GraphicsCaptureItem? item;
    private Direct3D11CaptureFramePool? framePool;
    private GraphicsCaptureSession? session;
    private nint activeWindowHandle;
    private bool disposed;
    private static int winRtInitialized;

    public async Task<Bitmap> CaptureAsync(nint windowHandle, TimeSpan timeout, CancellationToken token)
    {
        if (windowHandle == nint.Zero)
            throw new InvalidOperationException("WGC capture requires a valid HWND.");

        token.ThrowIfCancellationRequested();

        Direct3D11CaptureFrame? frame = null;
        lock (sync)
        {
            ThrowIfDisposed();
            EnsureSessionLocked(windowHandle);
            frame = DrainLatestFrameLocked();
        }

        if (frame == null)
        {
            var waitMs = Math.Max(1, (int)Math.Ceiling(timeout.TotalMilliseconds));
            await Task.Run(() => frameArrived.WaitOne(waitMs), token).ConfigureAwait(false);
            lock (sync)
            {
                ThrowIfDisposed();
                if (windowHandle == activeWindowHandle)
                    frame = DrainLatestFrameLocked();
            }
        }

        if (frame == null)
            throw new TimeoutException("WGC did not produce a frame before timeout.");

        using (frame)
        {
            var contentSize = frame.ContentSize;
            if (contentSize.Width <= 0 || contentSize.Height <= 0)
                throw new InvalidOperationException("WGC returned an empty frame.");

            var bitmap = await ConvertFrameToBitmapAsync(frame).ConfigureAwait(false);
            lock (sync)
            {
                if (!disposed &&
                    windowHandle == activeWindowHandle &&
                    item != null &&
                    (item.Size.Width != contentSize.Width || item.Size.Height != contentSize.Height))
                {
                    ResetSessionLocked();
                }
            }

            return bitmap;
        }
    }

    public void Dispose()
    {
        lock (sync)
        {
            if (disposed)
                return;

            disposed = true;
            ResetSessionLocked();
            ReleaseDeviceLocked();
        }

        frameArrived.Dispose();
    }

    private void EnsureSessionLocked(nint windowHandle)
    {
        if (session != null && framePool != null && item != null && activeWindowHandle == windowHandle)
            return;

        ResetSessionLocked();
        device ??= CreateDirect3DDevice();
        item = CreateCaptureItemForWindow(windowHandle);
        if (item.Size.Width <= 0 || item.Size.Height <= 0)
            throw new InvalidOperationException("WGC capture item has empty bounds.");

        framePool = Direct3D11CaptureFramePool.CreateFreeThreaded(
            device,
            DirectXPixelFormat.B8G8R8A8UIntNormalized,
            2,
            new SizeInt32 { Width = item.Size.Width, Height = item.Size.Height });
        framePool.FrameArrived += (_, _) => frameArrived.Set();

        session = framePool.CreateCaptureSession(item);
        try
        {
            session.IsCursorCaptureEnabled = false;
        }
        catch
        {
        }

        session.StartCapture();
        activeWindowHandle = windowHandle;
        frameArrived.Set();
    }

    private Direct3D11CaptureFrame? DrainLatestFrameLocked()
    {
        Direct3D11CaptureFrame? latest = null;
        while (framePool?.TryGetNextFrame() is { } frame)
        {
            latest?.Dispose();
            latest = frame;
        }

        return latest;
    }

    private void ResetSessionLocked()
    {
        try
        {
            (session as IDisposable)?.Dispose();
        }
        catch
        {
        }

        try
        {
            (framePool as IDisposable)?.Dispose();
        }
        catch
        {
        }

        session = null;
        framePool = null;
        item = null;
        activeWindowHandle = nint.Zero;
    }

    private void ReleaseDeviceLocked()
    {
        if (device is IDisposable disposable)
            disposable.Dispose();

        device = null;
    }

    private static async Task<Bitmap> ConvertFrameToBitmapAsync(Direct3D11CaptureFrame frame)
    {
        using var softwareBitmap = await SoftwareBitmap
            .CreateCopyFromSurfaceAsync(frame.Surface, BitmapAlphaMode.Ignore);
        using var stream = new InMemoryRandomAccessStream();
        var encoder = await BitmapEncoder.CreateAsync(BitmapEncoder.PngEncoderId, stream);
        encoder.SetSoftwareBitmap(softwareBitmap);
        await encoder.FlushAsync();

        stream.Seek(0);
        using var reader = new DataReader(stream.GetInputStreamAt(0));
        var size = checked((uint)stream.Size);
        await reader.LoadAsync(size);
        var bytes = new byte[size];
        reader.ReadBytes(bytes);

        using var memoryStream = new MemoryStream(bytes);
        using var decoded = new Bitmap(memoryStream);
        return decoded.Clone(new Rectangle(0, 0, decoded.Width, decoded.Height), PixelFormat.Format32bppArgb);
    }

    private static IDirect3DDevice CreateDirect3DDevice()
    {
        EnsureWinRtInitialized();

        var result = D3D11CreateDevice(
            nint.Zero,
            D3DDriverType.Hardware,
            nint.Zero,
            D3D11CreateDeviceBgraSupport,
            nint.Zero,
            0,
            D3D11SdkVersion,
            out var d3dDevice,
            out _,
            out var immediateContext);
        Marshal.ThrowExceptionForHR(result);

        nint dxgiDevice = nint.Zero;
        nint inspectable = nint.Zero;
        try
        {
            var dxgiDeviceGuid = IDXGIDeviceGuid;
            result = Marshal.QueryInterface(d3dDevice, in dxgiDeviceGuid, out dxgiDevice);
            Marshal.ThrowExceptionForHR(result);
            result = CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice, out inspectable);
            Marshal.ThrowExceptionForHR(result);
            return (IDirect3DDevice)Marshal.GetObjectForIUnknown(inspectable);
        }
        finally
        {
            if (inspectable != nint.Zero)
                Marshal.Release(inspectable);
            if (dxgiDevice != nint.Zero)
                Marshal.Release(dxgiDevice);
            if (immediateContext != nint.Zero)
                Marshal.Release(immediateContext);
            if (d3dDevice != nint.Zero)
                Marshal.Release(d3dDevice);
        }
    }

    private static GraphicsCaptureItem CreateCaptureItemForWindow(nint windowHandle)
    {
        EnsureWinRtInitialized();

        nint className = nint.Zero;
        nint factoryPointer = nint.Zero;
        var itemPointer = nint.Zero;
        try
        {
            const string graphicsCaptureItemClassName = "Windows.Graphics.Capture.GraphicsCaptureItem";
            var result = WindowsCreateString(graphicsCaptureItemClassName, graphicsCaptureItemClassName.Length, out className);
            Marshal.ThrowExceptionForHR(result);

            var interopGuid = IGraphicsCaptureItemInteropGuid;
            result = RoGetActivationFactory(className, ref interopGuid, out factoryPointer);
            Marshal.ThrowExceptionForHR(result);

            var interop = (IGraphicsCaptureItemInterop)Marshal.GetObjectForIUnknown(factoryPointer);
            var itemGuid = IGraphicsCaptureItemGuid;
            result = interop.CreateForWindow(windowHandle, ref itemGuid, out itemPointer);
            Marshal.ThrowExceptionForHR(result);
            return (GraphicsCaptureItem)Marshal.GetObjectForIUnknown(itemPointer);
        }
        finally
        {
            if (itemPointer != nint.Zero)
                Marshal.Release(itemPointer);
            if (factoryPointer != nint.Zero)
                Marshal.Release(factoryPointer);
            if (className != nint.Zero)
                WindowsDeleteString(className);
        }
    }

    private void ThrowIfDisposed()
    {
        if (disposed)
            throw new ObjectDisposedException(nameof(WindowsGraphicsWindowCapture));
    }

    private static void EnsureWinRtInitialized()
    {
        if (Interlocked.Exchange(ref winRtInitialized, 1) != 0)
            return;

        var result = RoInitialize(1);
        if (result < 0 && result != unchecked((int)0x80010106))
            Marshal.ThrowExceptionForHR(result);
    }

    private const uint D3D11CreateDeviceBgraSupport = 0x20;
    private const uint D3D11SdkVersion = 7;
    private static readonly Guid IGraphicsCaptureItemInteropGuid = new("3628E81B-3CAC-4C60-B7F4-23CE0E0C3356");

    [DllImport("combase.dll", ExactSpelling = true)]
    private static extern int RoInitialize(uint initType);

    [DllImport("combase.dll", ExactSpelling = true)]
    private static extern int RoGetActivationFactory(nint activatableClassId, ref Guid iid, out nint factory);

    [DllImport("combase.dll", ExactSpelling = true, CharSet = CharSet.Unicode)]
    private static extern int WindowsCreateString(string sourceString, int length, out nint hstring);

    [DllImport("combase.dll", ExactSpelling = true)]
    private static extern int WindowsDeleteString(nint hstring);

    [DllImport("d3d11.dll", ExactSpelling = true)]
    private static extern int D3D11CreateDevice(
        nint adapter,
        D3DDriverType driverType,
        nint software,
        uint flags,
        nint featureLevels,
        uint featureLevelsCount,
        uint sdkVersion,
        out nint device,
        out D3DFeatureLevel featureLevel,
        out nint immediateContext);

    [DllImport("d3d11.dll", ExactSpelling = true)]
    private static extern int CreateDirect3D11DeviceFromDXGIDevice(nint dxgiDevice, out nint graphicsDevice);

    [ComImport]
    [Guid("3628E81B-3CAC-4C60-B7F4-23CE0E0C3356")]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IGraphicsCaptureItemInterop
    {
        [PreserveSig]
        int CreateForWindow(nint window, ref Guid iid, out nint result);

        [PreserveSig]
        int CreateForMonitor(nint monitor, ref Guid iid, out nint result);
    }

    private enum D3DDriverType
    {
        Hardware = 1,
    }

    private enum D3DFeatureLevel
    {
        LevelUnknown = 0,
    }
}
