using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Threading.Tasks;

namespace VideoExplorer
{
    class VideoUtils
    {
        public static string GenerateThumbnail(string videoPath, string outputThumbnailPath, int frame, int width, bool bForceRegenerate = false)
        {
            if(!bForceRegenerate && File.Exists(outputThumbnailPath))
            {
                return frame > 1 ? Utils.AddPostfix(outputThumbnailPath, "001"): outputThumbnailPath;
            }

            //string vf = string.Format($"-vf \"select='not(mod(n\\,floor((N+1)/{0})))',scale={1}:-1\"", frame, width);
            string vf = string.Format("-vf \"fps=fps=1/60,scale={0}:-1\"", width);
            if (frame > 1)
            {
                outputThumbnailPath = Utils.AddPostfix(outputThumbnailPath, "%03d");
            }

            // FFmpeg 命令：从视频的第5秒提取一帧作为缩略图
            //string ffmpegCommand = $"-i \"{videoPath}\" {vf} -ss 00:00:05 -vframes {frame} \"{outputThumbnailPath}\"";
            string ffmpegCommand = $"-i \"{videoPath}\" {vf} -vframes {frame} \"{outputThumbnailPath}\"";

            // 启动 FFmpeg 进程
            ProcessStartInfo startInfo = new ProcessStartInfo
            {
                FileName = "ffmpeg",
                Arguments = ffmpegCommand,
                UseShellExecute = false,
                CreateNoWindow = false
            };

            using (Process process = new Process())
            {
                process.StartInfo = startInfo;
                process.Start();
                process.WaitForExit();

                if (process.ExitCode == 0)
                {
                    Console.WriteLine("缩略图生成成功！");
                }
                else
                {
                    string error = process.StandardError.ReadToEnd();
                    Console.WriteLine($"缩略图生成失败: {error}");
                }
            }
            return frame > 1 ? outputThumbnailPath.Replace("%03d", "001") : outputThumbnailPath;
        }
    }
}
