using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.IO.Compression;
using System.Linq;

namespace osuReplayEditor
{
    internal static class SkinManager
    {
        private const string CacheVersion = "skin-cache-v2";

        public static string PrepareSkinDirectory()
        {
            string oskPath = FindOsk();
            if (string.IsNullOrWhiteSpace(oskPath))
                return null;

            string cacheDir = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "osuReplayEditor",
                "skin",
                "current");
            Directory.CreateDirectory(cacheDir);

            string stampPath = Path.Combine(cacheDir, ".source");
            string sourceStamp = CacheVersion + "|" + oskPath + "|" + File.GetLastWriteTimeUtc(oskPath).Ticks + "|" + new FileInfo(oskPath).Length;
            if (File.Exists(stampPath) && File.ReadAllText(stampPath) == sourceStamp)
                return cacheDir;

            foreach (string file in Directory.EnumerateFiles(cacheDir))
                File.Delete(file);

            using (ZipArchive archive = ZipFile.OpenRead(oskPath))
            {
                ConvertTexture(archive, cacheDir, "hitcircle", 128, 128, "hitcircle.png", "hitcircle@2x.png");
                ConvertTexture(archive, cacheDir, "hitcircleoverlay", 128, 128, "hitcircleoverlay.png", "hitcircleoverlay@2x.png");
                ConvertTexture(archive, cacheDir, "approachcircle", 128, 128, "approachcircle.png", "approachcircle@2x.png");
                ConvertTexture(archive, cacheDir, "cursor", 32, 32, "cursor.png", "cursor@2x.png");
                ConvertTexture(archive, cacheDir, "slidertick", 16, 16, "slidertick.png", "slidertick@2x.png");
                CopyHitsounds(archive, cacheDir);
            }

            File.WriteAllText(stampPath, sourceStamp);
            return cacheDir;
        }

        private static string FindOsk()
        {
            string baseDir = AppDomain.CurrentDomain.BaseDirectory;
            string[] dirs =
            {
                Path.Combine(baseDir, "skin"),
                Path.GetFullPath(Path.Combine(baseDir, "..", "skin")),
                Path.Combine(Directory.GetCurrentDirectory(), "skin"),
            };

            return dirs
                .Where(Directory.Exists)
                .SelectMany(dir => Directory.EnumerateFiles(dir, "*.osk"))
                .OrderBy(path => path, StringComparer.OrdinalIgnoreCase)
                .FirstOrDefault();
        }

        private static void ConvertTexture(ZipArchive archive, string cacheDir, string outputName, int width, int height, params string[] candidates)
        {
            ZipArchiveEntry entry = FindEntry(archive, candidates);
            if (entry == null || entry.Length == 0)
                return;

            using (Stream stream = entry.Open())
            using (Bitmap source = new Bitmap(stream))
            using (Bitmap resized = new Bitmap(width, height, PixelFormat.Format32bppArgb))
            using (Graphics graphics = Graphics.FromImage(resized))
            {
                graphics.CompositingMode = CompositingMode.SourceCopy;
                graphics.CompositingQuality = CompositingQuality.HighQuality;
                graphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
                graphics.PixelOffsetMode = PixelOffsetMode.HighQuality;
                graphics.SmoothingMode = SmoothingMode.HighQuality;
                graphics.DrawImage(source, new Rectangle(0, 0, width, height));

                string outPath = Path.Combine(cacheDir, outputName + ".raw");
                using (FileStream output = File.Create(outPath))
                {
                    for (int y = 0; y < height; ++y)
                    {
                        for (int x = 0; x < width; ++x)
                        {
                            Color c = resized.GetPixel(x, y);
                            output.WriteByte(c.R);
                            output.WriteByte(c.G);
                            output.WriteByte(c.B);
                            output.WriteByte(c.A);
                        }
                    }
                }
            }
        }

        private static void CopyHitsounds(ZipArchive archive, string cacheDir)
        {
            string[] sets = { "normal", "soft", "drum" };
            string[] names = { "hitnormal", "hitwhistle", "hitfinish", "hitclap" };

            foreach (string set in sets)
            {
                foreach (string name in names)
                {
                    ZipArchiveEntry entry = FindEntry(archive,
                        set + "-" + name + ".wav",
                        set + "-" + name + ".mp3",
                        set + "-" + name + ".ogg");
                    if (entry == null || entry.Length == 0)
                        continue;

                    string extension = Path.GetExtension(entry.FullName);
                    string outPath = Path.Combine(cacheDir, set + "-" + name + extension.ToLowerInvariant());
                    using (Stream input = entry.Open())
                    using (FileStream output = File.Create(outPath))
                        input.CopyTo(output);
                }
            }
        }

        private static ZipArchiveEntry FindEntry(ZipArchive archive, params string[] candidates)
        {
            foreach (string candidate in candidates)
            {
                ZipArchiveEntry entry = archive.Entries.FirstOrDefault(e =>
                    string.Equals(Path.GetFileName(e.FullName), candidate, StringComparison.OrdinalIgnoreCase));
                if (entry != null)
                    return entry;
            }
            return null;
        }
    }
}
