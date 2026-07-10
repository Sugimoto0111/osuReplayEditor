using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Net;
using System.Runtime.Serialization;
using System.Runtime.Serialization.Json;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;

namespace osuReplayEditor
{
    internal static class ReplayBeatmapResolver
    {
        private static readonly Regex Md5Pattern = new Regex("^[0-9a-fA-F]{32}$", RegexOptions.Compiled);

        [DataContract]
        private sealed class BeatmapApiResult
        {
            [DataMember(Name = "beatmap_id")]
            public string BeatmapId { get; set; }

            [DataMember(Name = "beatmapset_id")]
            public string BeatmapSetId { get; set; }
        }

        public static string TryResolveBeatmapFile(string replayPath, string apiKey, out string warning)
        {
            warning = null;

            string beatmapMd5;
            try
            {
                beatmapMd5 = ReadBeatmapMd5(replayPath);
            }
            catch (Exception e)
            {
                warning = "Could not read beatmap MD5 from the replay:\n\n" + e.Message;
                return null;
            }

            string cacheRoot = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "osuReplayEditor");
            string beatmapCacheDir = Path.Combine(cacheRoot, "beatmaps");
            string beatmapSetCacheDir = Path.Combine(cacheRoot, "beatmapsets");
            string oszCacheDir = Path.Combine(cacheRoot, "osz");
            Directory.CreateDirectory(beatmapCacheDir);
            Directory.CreateDirectory(beatmapSetCacheDir);
            Directory.CreateDirectory(oszCacheDir);

            string normalizedMd5 = beatmapMd5.ToLowerInvariant();
            string extractedOsuPath = FindExtractedBeatmapByMd5(beatmapSetCacheDir, normalizedMd5);
            if (!string.IsNullOrWhiteSpace(extractedOsuPath) && AudioFileExists(extractedOsuPath))
                return extractedOsuPath;

            string cachedOsuPath = Path.Combine(beatmapCacheDir, normalizedMd5 + ".osu");
            if (File.Exists(cachedOsuPath) && AudioFileExists(cachedOsuPath))
                return cachedOsuPath;

            if (string.IsNullOrWhiteSpace(apiKey))
            {
                if (File.Exists(cachedOsuPath))
                    return cachedOsuPath;
                if (!string.IsNullOrWhiteSpace(extractedOsuPath))
                    return extractedOsuPath;

                warning = "No cached .osu file was found for this replay.\n\nSet osu_api_key in Settings to download beatmaps by replay MD5.";
                return null;
            }

            try
            {
                ServicePointManager.SecurityProtocol |= SecurityProtocolType.Tls12;
                using (var client = new WebClient())
                {
                    client.Headers[HttpRequestHeader.UserAgent] = "osuReplayEditor";
                    string queryUrl = "https://osu.ppy.sh/api/get_beatmaps?k=" +
                                      Uri.EscapeDataString(apiKey) +
                                      "&h=" +
                                      Uri.EscapeDataString(beatmapMd5);
                    string json = client.DownloadString(queryUrl);
                    BeatmapApiResult beatmap = ParseBeatmap(json);
                    if (beatmap == null || string.IsNullOrWhiteSpace(beatmap.BeatmapId))
                    {
                        warning = "osu! API did not return a beatmap for replay MD5:\n\n" + beatmapMd5;
                        return null;
                    }

                    string oszOsuPath = TryResolveFromOsz(beatmap.BeatmapSetId, normalizedMd5, beatmapSetCacheDir, oszCacheDir, client);
                    if (!string.IsNullOrWhiteSpace(oszOsuPath))
                        return oszOsuPath;

                    client.DownloadFile("https://osu.ppy.sh/osu/" + beatmap.BeatmapId, cachedOsuPath);
                    return cachedOsuPath;
                }
            }
            catch (Exception e)
            {
                warning = "Could not download the .osu file for this replay:\n\n" + e.Message;
                return null;
            }
        }

        private static BeatmapApiResult ParseBeatmap(string json)
        {
            using (var stream = new MemoryStream(Encoding.UTF8.GetBytes(json)))
            {
                var serializer = new DataContractJsonSerializer(typeof(List<BeatmapApiResult>));
                var results = (List<BeatmapApiResult>)serializer.ReadObject(stream);
                if (results == null || results.Count == 0)
                    return null;
                return results[0];
            }
        }

        private static string TryResolveFromOsz(string beatmapSetId, string beatmapMd5, string beatmapSetCacheDir, string oszCacheDir, WebClient client)
        {
            if (string.IsNullOrWhiteSpace(beatmapSetId))
                return null;

            string safeSetId = MakeSafePathPart(beatmapSetId);
            string setDir = Path.Combine(beatmapSetCacheDir, safeSetId);
            Directory.CreateDirectory(setDir);

            string extracted = FindBeatmapByMd5(setDir, beatmapMd5);
            if (!string.IsNullOrWhiteSpace(extracted) && AudioFileExists(extracted))
                return extracted;

            string oszPath = Path.Combine(oszCacheDir, safeSetId + ".osz");
            if (!File.Exists(oszPath) || !IsZipFile(oszPath))
            {
                if (!TryDownloadOsz(beatmapSetId, oszPath, client))
                    return extracted;
            }

            try
            {
                ExtractOsz(oszPath, setDir);
            }
            catch
            {
                return extracted;
            }

            extracted = FindBeatmapByMd5(setDir, beatmapMd5);
            return extracted;
        }

        private static bool TryDownloadOsz(string beatmapSetId, string oszPath, WebClient client)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(oszPath));
            string[] urlTemplates =
            {
                "https://osu.ppy.sh/beatmapsets/{0}/download?noVideo=1",
                "https://osu.ppy.sh/d/{0}n",
                "https://api.nerinyan.moe/d/{0}?nv=1",
                "https://catboy.best/d/{0}"
            };

            foreach (string template in urlTemplates)
            {
                string tempPath = oszPath + ".download";
                try
                {
                    if (File.Exists(tempPath))
                        File.Delete(tempPath);

                    client.DownloadFile(string.Format(template, Uri.EscapeDataString(beatmapSetId)), tempPath);
                    if (!IsZipFile(tempPath))
                        continue;

                    if (File.Exists(oszPath))
                        File.Delete(oszPath);
                    File.Move(tempPath, oszPath);
                    return true;
                }
                catch
                {
                }
                finally
                {
                    try
                    {
                        if (File.Exists(tempPath))
                            File.Delete(tempPath);
                    }
                    catch
                    {
                    }
                }
            }

            return false;
        }

        private static void ExtractOsz(string oszPath, string destinationDir)
        {
            string root = Path.GetFullPath(destinationDir);
            if (!root.EndsWith(Path.DirectorySeparatorChar.ToString()))
                root += Path.DirectorySeparatorChar;

            using (ZipArchive archive = ZipFile.OpenRead(oszPath))
            {
                foreach (ZipArchiveEntry entry in archive.Entries)
                {
                    if (string.IsNullOrWhiteSpace(entry.Name))
                        continue;

                    string relativePath = entry.FullName.Replace('/', Path.DirectorySeparatorChar).Replace('\\', Path.DirectorySeparatorChar);
                    string fullPath = Path.GetFullPath(Path.Combine(root, relativePath));
                    if (!fullPath.StartsWith(root, StringComparison.OrdinalIgnoreCase))
                        continue;

                    Directory.CreateDirectory(Path.GetDirectoryName(fullPath));
                    entry.ExtractToFile(fullPath, true);
                }
            }
        }

        private static string FindExtractedBeatmapByMd5(string beatmapSetCacheDir, string beatmapMd5)
        {
            if (!Directory.Exists(beatmapSetCacheDir))
                return null;

            return FindBeatmapByMd5(beatmapSetCacheDir, beatmapMd5);
        }

        private static string FindBeatmapByMd5(string directory, string beatmapMd5)
        {
            if (!Directory.Exists(directory))
                return null;

            foreach (string osuPath in Directory.EnumerateFiles(directory, "*.osu", SearchOption.AllDirectories))
            {
                try
                {
                    if (string.Equals(GetFileMd5(osuPath), beatmapMd5, StringComparison.OrdinalIgnoreCase))
                        return osuPath;
                }
                catch
                {
                }
            }

            return null;
        }

        private static string GetFileMd5(string path)
        {
            using (MD5 md5 = MD5.Create())
            using (FileStream stream = File.OpenRead(path))
            {
                byte[] hash = md5.ComputeHash(stream);
                StringBuilder builder = new StringBuilder(hash.Length * 2);
                foreach (byte value in hash)
                    builder.Append(value.ToString("x2"));
                return builder.ToString();
            }
        }

        private static bool AudioFileExists(string osuPath)
        {
            string audioFilename = ReadAudioFilename(osuPath);
            if (string.IsNullOrWhiteSpace(audioFilename))
                return false;

            string audioPath = Path.IsPathRooted(audioFilename)
                ? audioFilename
                : Path.Combine(Path.GetDirectoryName(osuPath), audioFilename);
            return File.Exists(audioPath);
        }

        private static string ReadAudioFilename(string osuPath)
        {
            bool inGeneralSection = false;
            foreach (string rawLine in File.ReadLines(osuPath, Encoding.UTF8))
            {
                string line = rawLine.Trim();
                if (line.Length == 0 || line.StartsWith("//"))
                    continue;

                if (line.StartsWith("[") && line.EndsWith("]"))
                {
                    inGeneralSection = string.Equals(line, "[General]", StringComparison.OrdinalIgnoreCase);
                    continue;
                }

                if (!inGeneralSection)
                    continue;

                int separator = line.IndexOf(':');
                if (separator < 0)
                    continue;

                string key = line.Substring(0, separator).Trim();
                if (!string.Equals(key, "AudioFilename", StringComparison.OrdinalIgnoreCase))
                    continue;

                return line.Substring(separator + 1).Trim();
            }

            return null;
        }

        private static bool IsZipFile(string path)
        {
            try
            {
                using (FileStream stream = File.OpenRead(path))
                {
                    if (stream.Length < 4)
                        return false;

                    int b0 = stream.ReadByte();
                    int b1 = stream.ReadByte();
                    return b0 == 'P' && b1 == 'K';
                }
            }
            catch
            {
                return false;
            }
        }

        private static string MakeSafePathPart(string value)
        {
            foreach (char invalid in Path.GetInvalidFileNameChars())
                value = value.Replace(invalid, '_');
            return value;
        }

        private static string ReadBeatmapMd5(string replayPath)
        {
            using (var stream = File.OpenRead(replayPath))
            using (var reader = new BinaryReader(stream, Encoding.UTF8))
            {
                reader.ReadByte();  // game mode
                reader.ReadInt32(); // game version
                string md5 = ReadOsuString(reader);
                if (!Md5Pattern.IsMatch(md5))
                    throw new InvalidDataException("Replay beatmap MD5 field is not a valid MD5 hash.");
                return md5;
            }
        }

        private static string ReadOsuString(BinaryReader reader)
        {
            byte marker = reader.ReadByte();
            if (marker == 0x00)
                return string.Empty;
            if (marker != 0x0b)
                throw new InvalidDataException("Unexpected osu! string marker in replay file.");

            int length = ReadUleb128(reader);
            byte[] bytes = reader.ReadBytes(length);
            if (bytes.Length != length)
                throw new EndOfStreamException("Replay ended while reading beatmap MD5.");
            return Encoding.UTF8.GetString(bytes);
        }

        private static int ReadUleb128(BinaryReader reader)
        {
            int value = 0;
            int shift = 0;
            while (shift < 35)
            {
                byte b = reader.ReadByte();
                value |= (b & 0x7f) << shift;
                if ((b & 0x80) == 0)
                    return value;
                shift += 7;
            }
            throw new InvalidDataException("Invalid osu! string length.");
        }
    }
}
