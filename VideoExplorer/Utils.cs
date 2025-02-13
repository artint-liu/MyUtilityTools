using System.Diagnostics;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;

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

        public static List<string> GenerateSequentialFiles(string filepath)
        {
            var result = new List<string>();

            // 检查初始文件是否存在
            if (!File.Exists(filepath))
            {
                return result;
            }

            // 解析路径组成部分
            string directory = Path.GetDirectoryName(filepath);
            string fileName = Path.GetFileNameWithoutExtension(filepath);
            string extension = Path.GetExtension(filepath);

            // 使用正则匹配数字部分
            var match = Regex.Match(fileName, @"\.(\d+)$");
            if (!match.Success) return result;

            // 获取前缀和初始编号
            string prefix = fileName.Substring(0, match.Index);
            //int startNumber = int.Parse(match.Groups[1].Value);
            int digits = match.Groups[1].Length;  // 保持原始数字位数

            // 生成后续文件
            int currentNumber = 1;
            while (true)
            {
                // 格式化新文件名（保持前导零）
                string newFileName = $"{prefix}.{currentNumber.ToString($"D{digits}")}{extension}";
                string fullPath = Path.Combine(directory, newFileName);

                if (File.Exists(fullPath))
                {
                    result.Add(fullPath);
                    currentNumber++;
                }
                else
                {
                    break;
                }
            }

            return result;
        }

        public static void OpenFileWithDefaultProgram(string filePath)
        {
            try
            {
                // 使用默认程序打开文件
                Process.Start(new ProcessStartInfo(filePath) { UseShellExecute = true });
                Console.WriteLine("文件已使用默认程序打开。");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"无法打开文件: {ex.Message}");
            }
        }

        public static void OpenFileInExplorer(string filePath)
        {
            try
            {
                // 获取文件所在文件夹路径
                string folderPath = Path.GetDirectoryName(filePath);

                // 使用资源管理器打开文件夹
                Process.Start("explorer.exe", folderPath);
                Console.WriteLine("文件所在文件夹已打开。");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"无法打开文件夹: {ex.Message}");
            }
        }

        public static string NormalizeCategory(string categoryText)
        {
            string[] categorys = categoryText.Split(new char[] { ',', '，', ' ', '\t', '/', '\\' });
            StringBuilder stringBuilder = new StringBuilder();
            foreach (string category in categorys)
            {
                if(category != string.Empty)
                    stringBuilder.Append(category).Append(',');
            }
            return stringBuilder.ToString().TrimEnd(',');
        }
    }
}
