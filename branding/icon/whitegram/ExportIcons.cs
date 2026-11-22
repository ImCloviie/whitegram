using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;

// Production packaging of the approved raster artwork; preserve its aspect ratio.
public static class WhitegramIconExport {
    public static RectangleF DesktopPlacement(Bitmap mark, int size) {
        // The supplied 640 px AyuGram reference has a 312 x 312 px silhouette,
        // centered at (300, 341). Fit uniformly, including the W cutout.
        int left = mark.Width, top = mark.Height, right = -1, bottom = -1;
        for (int y = 0; y < mark.Height; ++y) {
            for (int x = 0; x < mark.Width; ++x) {
                if (mark.GetPixel(x, y).A < 128) continue;
                left = Math.Min(left, x);
                top = Math.Min(top, y);
                right = Math.Max(right, x);
                bottom = Math.Max(bottom, y);
            }
        }
        if (right < left || bottom < top) throw new ArgumentException("Empty icon mark.");
        float scale = Math.Min(size * (312f / 640) / (right - left + 1),
            size * (312f / 640) / (bottom - top + 1));
        return new RectangleF(size * (300f / 640) - (left + right + 1) * scale / 2,
            size * (341f / 640) - (top + bottom + 1) * scale / 2,
            mark.Width * scale, mark.Height * scale);
    }

    public static Bitmap LoadMark(string file, bool dark, bool sourceDark) {
        using (var source = new Bitmap(file)) {
            var mark = new Bitmap(source.Width, source.Height, PixelFormat.Format32bppArgb);
            for (int y = 0; y < source.Height; ++y) {
                for (int x = 0; x < source.Width; ++x) {
                    var pixel = source.GetPixel(x, y);
                    int luminance = (pixel.R + pixel.G + pixel.B) / 3;
                    int coverage = sourceDark ? luminance : 255 - luminance;
                    // Remove compression/generation haze while retaining antialiasing.
                    coverage = Math.Max(0, Math.Min(255, (coverage - 12) * 255 / 231));
                    int color = dark ? 255 : 0;
                    mark.SetPixel(x, y, Color.FromArgb(coverage, color, color, color));
                }
            }
            return mark;
        }
    }

    public static Bitmap Render(Bitmap mark, int size, bool dark, string shape) {
        // Supersample the platform mask as well as the artwork at small sizes.
        int renderSize = Math.Max(size, 1024);
        using (var large = new Bitmap(renderSize, renderSize, PixelFormat.Format32bppArgb)) {
            using (var g = Graphics.FromImage(large)) {
                g.Clear(Color.Transparent);
                g.SmoothingMode = SmoothingMode.AntiAlias;
                g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                g.PixelOffsetMode = PixelOffsetMode.HighQuality;
                if (shape != "foreground") {
                    using (var path = new GraphicsPath()) {
                        float edge = renderSize - 1;
                        if (shape == "circle" || shape == "desktop") {
                            path.AddEllipse(0.5f, 0.5f, edge, edge);
                        } else {
                            float d = renderSize * 0.4f;
                            path.AddArc(0.5f, 0.5f, d, d, 180, 90);
                            path.AddArc(edge - d, 0.5f, d, d, 270, 90);
                            path.AddArc(edge - d, edge - d, d, d, 0, 90);
                            path.AddArc(0.5f, edge - d, d, d, 90, 90);
                            path.CloseFigure();
                        }
                        using (var brush = new SolidBrush(dark ? Color.Black : Color.White)) {
                            g.FillPath(brush, path);
                        }
                    }
                }
                var placement = shape == "desktop"
                    ? DesktopPlacement(mark, renderSize)
                    : new RectangleF(0, 0, renderSize, renderSize);
                g.DrawImage(mark, placement);
            }
            var result = new Bitmap(size, size, PixelFormat.Format32bppArgb);
            using (var g = Graphics.FromImage(result)) {
                g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                g.PixelOffsetMode = PixelOffsetMode.HighQuality;
                g.DrawImage(large, new Rectangle(0, 0, size, size));
            }
            return result;
        }
    }

    public static void Png(Bitmap mark, int size, bool dark, string shape, string file) {
        Directory.CreateDirectory(Path.GetDirectoryName(file));
        using (var image = Render(mark, size, dark, shape)) image.Save(file, ImageFormat.Png);
    }

    public static void Ico(Bitmap mark, bool dark, string file) {
        int[] sizes = { 16, 20, 24, 32, 40, 48, 64, 96, 128, 256 };
        var frames = new List<byte[]>();
        foreach (int size in sizes) {
            using (var image = Render(mark, size, dark, "desktop"))
            using (var stream = new MemoryStream()) {
                image.Save(stream, ImageFormat.Png);
                frames.Add(stream.ToArray());
            }
        }
        Directory.CreateDirectory(Path.GetDirectoryName(file));
        using (var writer = new BinaryWriter(File.Create(file))) {
            writer.Write((ushort)0);
            writer.Write((ushort)1);
            writer.Write((ushort)sizes.Length);
            int offset = 6 + sizes.Length * 16;
            for (int i = 0; i < sizes.Length; ++i) {
                writer.Write((byte)(sizes[i] == 256 ? 0 : sizes[i]));
                writer.Write((byte)(sizes[i] == 256 ? 0 : sizes[i]));
                writer.Write((byte)0);
                writer.Write((byte)0);
                writer.Write((ushort)1);
                writer.Write((ushort)32);
                writer.Write(frames[i].Length);
                writer.Write(offset);
                offset += frames[i].Length;
            }
            foreach (var frame in frames) writer.Write(frame);
        }
    }
}
