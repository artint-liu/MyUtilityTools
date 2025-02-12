using System.IO;
using System.Security.Cryptography;

namespace VideoExplorer
{
    static class Utils
    {
        public static string GenerateSHA1(string filePath)
        {
            using (var sha1 = SHA1.Create())
            using (var stream = File.OpenRead(filePath))
            {
                byte[] hashBytes = sha1.ComputeHash(stream);
                return BitConverter.ToString(hashBytes).Replace("-", "").ToLowerInvariant();
            }
        }

        public static async Task<string> GenerateSHA1Async(string filePath, IProgress<float> progress)
        {
            const int bufferSize = 1024 * 1024; // 每次读取 1MB
            byte[] buffer = new byte[bufferSize];
            int bytesRead;

            using (var sha1 = SHA1.Create())
            using (var stream = new FileStream(filePath, FileMode.Open, FileAccess.Read, FileShare.Read, bufferSize, true))
            {
                long totalBytes = stream.Length;
                long totalBytesRead = 0;

                while ((bytesRead = await stream.ReadAsync(buffer, 0, buffer.Length)) > 0)
                {
                    sha1.TransformBlock(buffer, 0, bytesRead, null, 0);
                    totalBytesRead += bytesRead;

                    // 更新进度
                    float progressPercentage = (float)totalBytesRead / totalBytes;
                    progress?.Report(progressPercentage);
                }

                sha1.TransformFinalBlock(buffer, 0, 0);
                return BitConverter.ToString(sha1.Hash).Replace("-", "").ToLowerInvariant();
            }
        }

        public static string AddPostfix(string filename, string postfix)
        {
            string extension = Path.GetExtension(filename);
            return Path.ChangeExtension(filename, $"{postfix}{extension}");
        }
    }
}
