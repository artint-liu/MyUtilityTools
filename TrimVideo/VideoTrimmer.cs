using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading.Tasks;

namespace TrimVideo
{
    public class VideoInfo
    {
        public double Duration    { get; set; }
        public double FrameRate   { get; set; }
        public int    Width       { get; set; }
        public int    Height      { get; set; }
        public string VideoCodec  { get; set; } = "";
        public string AudioCodec  { get; set; } = "";
        public long   FileSizeBytes { get; set; }
    }

    /// <summary>
    /// 通过 FFmpeg DLL 实现：
    ///   1. GetVideoInfo  — 读取视频元数据
    ///   2. TrimAsync     — 无损 remux（-c copy）
    /// </summary>
    public class VideoTrimmer
    {
        private readonly string _ffmpegDir;

        public VideoTrimmer(string ffmpegDir)
        {
            _ffmpegDir = ffmpegDir;
            NativeLibraryLoader.EnsureLoaded(ffmpegDir);
            FF.av_log_set_level(16);
        }

        public bool IsAvailable =>
            File.Exists(Path.Combine(_ffmpegDir, "avformat-62.dll"));

        // ─────────────────────────────────────────────────────────
        //  查找目标时间之前最近的 I 帧（关键帧）时间戳
        // ─────────────────────────────────────────────────────────
        public double? FindPrevKeyFrameTime(string filePath, double targetSeconds)
        {
            IntPtr fmtCtx  = IntPtr.Zero;
            IntPtr nullDict = IntPtr.Zero;

            try
            {
                if (FF.avformat_open_input(ref fmtCtx, filePath, IntPtr.Zero, ref nullDict) < 0)
                    return null;
                if (FF.avformat_find_stream_info(fmtCtx, IntPtr.Zero) < 0)
                    return null;

                // 找视频流
                int nb = FmtCtx.NbStreams(fmtCtx);
                int videoIdx = -1;
                for (int i = 0; i < nb; i++)
                {
                    IntPtr stream = FmtCtx.Stream(fmtCtx, i);
                    IntPtr par    = StreamCtx.CodecPar(stream);
                    if (CodecParCtx.CodecType(par) == FF.AVMEDIA_TYPE_VIDEO)
                    { videoIdx = i; break; }
                }
                if (videoIdx < 0) return null;

                IntPtr vStream = FmtCtx.Stream(fmtCtx, videoIdx);
                var tb = StreamCtx.TimeBase(vStream);

                // 向前回退最多 30 秒来寻找 I 帧（大多数 GOP 不超过此范围）
                double seekBack = Math.Min(targetSeconds, 30.0);
                double seekTime = targetSeconds - seekBack;
                if (seekTime > 0.1)
                {
                    long seekTs = (long)(seekTime * FF.AV_TIME_BASE);
                    FF.avformat_seek_file(fmtCtx, -1, long.MinValue, seekTs, seekTs, 0);
                }
                else
                {
                    FF.avformat_seek_file(fmtCtx, -1, long.MinValue, 0, 0, 0);
                }

                IntPtr pkt = FF.av_packet_alloc();
                double lastKeyFrameSec = 0;
                bool found = false;

                try
                {
                    while (true)
                    {
                        FF.av_packet_unref(pkt);
                        int r = FF.av_read_frame(fmtCtx, pkt);
                        if (r < 0) break;

                        if (PktCtx.GetStreamIndex(pkt) != videoIdx)
                            continue;

                        long pts = PktCtx.GetPts(pkt);
                        long dts = PktCtx.GetDts(pkt);
                        long refTs = pts != FF.AV_NOPTS_VALUE ? pts : dts;
                        double pktSec = (refTs != FF.AV_NOPTS_VALUE) ? refTs * tb.ToDouble() : -1;

                        if (pktSec < 0) continue;

                        // 超过目标时间则停止
                        if (pktSec > targetSeconds + 0.5) break;

                        // 检查是否为关键帧
                        if (PktCtx.IsKeyFrame(pkt) && pktSec <= targetSeconds + 0.01)
                        {
                            lastKeyFrameSec = pktSec;
                            found = true;
                        }
                    }
                }
                finally
                {
                    FF.av_packet_free(ref pkt);
                }

                return found ? lastKeyFrameSec : null;
            }
            catch
            {
                return null;
            }
            finally
            {
                if (fmtCtx != IntPtr.Zero) FF.avformat_close_input(ref fmtCtx);
            }
        }

        // ─────────────────────────────────────────────────────────
        public VideoInfo? GetVideoInfo(string filePath)
        {
            IntPtr fmtCtx  = IntPtr.Zero;
            IntPtr nullDict = IntPtr.Zero;

            try
            {
                if (FF.avformat_open_input(ref fmtCtx, filePath, IntPtr.Zero, ref nullDict) < 0)
                    return null;
                if (FF.avformat_find_stream_info(fmtCtx, IntPtr.Zero) < 0)
                    return null;

                var info = new VideoInfo
                {
                    FileSizeBytes = new FileInfo(filePath).Length
                };

                long durUs = FmtCtx.Duration(fmtCtx);
                info.Duration = durUs > 0 ? durUs / (double)FF.AV_TIME_BASE : 0;

                int nb = FmtCtx.NbStreams(fmtCtx);
                for (int i = 0; i < nb; i++)
                {
                    IntPtr stream  = FmtCtx.Stream(fmtCtx, i);
                    IntPtr par     = StreamCtx.CodecPar(stream);
                    int    ctype   = CodecParCtx.CodecType(par);

                    if (ctype == FF.AVMEDIA_TYPE_VIDEO && info.VideoCodec == "")
                    {
                        info.Width  = CodecParCtx.Width(par);
                        info.Height = CodecParCtx.Height(par);
                        info.VideoCodec = GetCodecName(CodecParCtx.CodecId(par));

                        var fps = StreamCtx.AvgFrameRate(stream);
                        info.FrameRate = fps.ToDouble() > 0 ? fps.ToDouble() : 25.0;

                        // 若容器时长为0，尝试从流时长补充
                        if (info.Duration <= 0)
                        {
                            long sdur = StreamCtx.Duration(stream);
                            var tb = StreamCtx.TimeBase(stream);
                            if (sdur > 0 && tb.den != 0)
                                info.Duration = sdur * tb.ToDouble();
                        }
                    }
                    else if (ctype == FF.AVMEDIA_TYPE_AUDIO && info.AudioCodec == "")
                    {
                        info.AudioCodec = GetCodecName(CodecParCtx.CodecId(par));
                    }
                }

                return info;
            }
            catch
            {
                return null;
            }
            finally
            {
                if (fmtCtx != IntPtr.Zero) FF.avformat_close_input(ref fmtCtx);
            }
        }

        // ─────────────────────────────────────────────────────────
        //  无损 Remux（纯 DLL，不重新编码）
        // ─────────────────────────────────────────────────────────
        public Task<(bool Success, string Message)> TrimAsync(
            string inputPath,
            string outputPath,
            double startSeconds,
            double endSeconds,
            IProgress<double>? progress = null)
        {
            return Task.Run(() => DoTrim(inputPath, outputPath, startSeconds, endSeconds, progress));
        }

        private (bool Success, string Message) DoTrim(
            string inputPath,
            string outputPath,
            double startSeconds,
            double endSeconds,
            IProgress<double>? progress)
        {
            double duration = endSeconds - startSeconds;
            if (duration <= 0)
                return (false, "结束时间必须大于开始时间");

            IntPtr inCtx    = IntPtr.Zero;
            IntPtr outCtx   = IntPtr.Zero;
            IntPtr pkt      = IntPtr.Zero;
            IntPtr nullDict = IntPtr.Zero;

            try
            {
                progress?.Report(0.01);

                // ── 打开输入 ─────────────────────────────────────
                if (FF.avformat_open_input(ref inCtx, inputPath, IntPtr.Zero, ref nullDict) < 0)
                    return (false, "无法打开输入文件");
                if (FF.avformat_find_stream_info(inCtx, IntPtr.Zero) < 0)
                    return (false, "无法读取流信息");

                int nbIn = FmtCtx.NbStreams(inCtx);

                // ── 创建输出容器 ──────────────────────────────────
                if (FF.avformat_alloc_output_context2(ref outCtx, IntPtr.Zero, null, outputPath) < 0)
                    return (false, "无法创建输出容器");

                // 映射流：输入流 i → 输出流 streamMap[i] (-1=丢弃)
                int[] streamMap = new int[nbIn];
                int outStreamIdx = 0;

                for (int i = 0; i < nbIn; i++)
                {
                    IntPtr inStream  = FmtCtx.Stream(inCtx, i);
                    IntPtr inPar     = StreamCtx.CodecPar(inStream);
                    int    ctype     = CodecParCtx.CodecType(inPar);

                    // 只保留视频 & 音频
                    if (ctype != FF.AVMEDIA_TYPE_VIDEO && ctype != FF.AVMEDIA_TYPE_AUDIO)
                    {
                        streamMap[i] = -1;
                        continue;
                    }

                    streamMap[i] = outStreamIdx++;
                    IntPtr outStream = FF.avformat_new_stream(outCtx, IntPtr.Zero);
                    if (outStream == IntPtr.Zero)
                        return (false, "无法创建输出流");

                    // 复制编解码参数
                    IntPtr outPar = StreamCtx.CodecPar(outStream);
                    FF.avcodec_parameters_copy(outPar, inPar);
                }

                // ── 打开输出 IO ───────────────────────────────────
                IntPtr ioCtx = IntPtr.Zero;
                if (FF.avio_open(ref ioCtx, outputPath, FF.AVIO_FLAG_WRITE) < 0)
                    return (false, "无法打开输出文件");

                // 把 ioCtx 写到 outCtx->pb 字段（偏移 32）
                Marshal.WriteIntPtr(outCtx, 32, ioCtx);

                IntPtr outOpts = IntPtr.Zero;
                if (FF.avformat_write_header(outCtx, ref outOpts) < 0)
                    return (false, "写文件头失败");

                // ── Seek 到起始点 ─────────────────────────────────
                long seekTs = (long)(startSeconds * FF.AV_TIME_BASE);
                FF.avformat_seek_file(inCtx, -1, long.MinValue, seekTs, seekTs, 0);

                pkt = FF.av_packet_alloc();
                // ── 读包 & 写包 ───────────────────────────────────
                long[] dtsOffset = new long[nbIn]; // 每路流的 DTS 偏移
                for (int i = 0; i < nbIn; i++) dtsOffset[i] = FF.AV_NOPTS_VALUE;

                while (true)
                {
                    FF.av_packet_unref(pkt);
                    int r = FF.av_read_frame(inCtx, pkt);
                    if (r < 0) break; // EOF

                    int si = PktCtx.GetStreamIndex(pkt);
                    if (si < 0 || si >= nbIn || streamMap[si] < 0)
                    {
                        FF.av_packet_unref(pkt);
                        continue;
                    }

                    IntPtr inStream = FmtCtx.Stream(inCtx, si);
                    var inTb = StreamCtx.TimeBase(inStream);

                    // 计算当前包对应时间（秒）
                    long pts = PktCtx.GetPts(pkt);
                    long dts = PktCtx.GetDts(pkt);
                    long refTs = pts != FF.AV_NOPTS_VALUE ? pts : dts;
                    double pktSec = (refTs != FF.AV_NOPTS_VALUE) ? refTs * inTb.ToDouble() : 0;

                    // 跳过起始点之前的包
                    if (pktSec < startSeconds - 0.1)
                    {
                        FF.av_packet_unref(pkt);
                        continue;
                    }
                    // 停止读取超出结束时间的包
                    if (pktSec >= endSeconds)
                    {
                        FF.av_packet_unref(pkt);
                        break;
                    }

                    // 记录每路流的起始偏移（用于归零时间戳）
                    if (dtsOffset[si] == FF.AV_NOPTS_VALUE)
                        dtsOffset[si] = dts != FF.AV_NOPTS_VALUE ? dts : pts;

                    // 重新计算时间戳（相对于裁剪起始点归零）
                    IntPtr outStream = FmtCtx.Stream(outCtx, streamMap[si]);
                    var outTb = StreamCtx.TimeBase(outStream);

                    if (pts != FF.AV_NOPTS_VALUE && dtsOffset[si] != FF.AV_NOPTS_VALUE)
                    {
                        long newPts = RescaleTs(pts - dtsOffset[si], inTb, outTb);
                        PktCtx.SetPts(pkt, newPts);
                    }
                    if (dts != FF.AV_NOPTS_VALUE && dtsOffset[si] != FF.AV_NOPTS_VALUE)
                    {
                        long newDts = RescaleTs(dts - dtsOffset[si], inTb, outTb);
                        PktCtx.SetDts(pkt, newDts);
                    }
                    PktCtx.SetStreamIndex(pkt, streamMap[si]);

                    FF.av_interleaved_write_frame(outCtx, pkt);

                    // 汇报进度
                    double prog = Math.Min((pktSec - startSeconds) / duration, 0.99);
                    progress?.Report(prog);
                }

                FF.av_write_trailer(outCtx);
                progress?.Report(1.0);

                return (true, $"裁剪完成：{outputPath}");
            }
            catch (Exception ex)
            {
                return (false, ex.Message);
            }
            finally
            {
                if (pkt    != IntPtr.Zero) FF.av_packet_free(ref pkt);
                if (outCtx != IntPtr.Zero)
                {
                    IntPtr pb = Marshal.ReadIntPtr(outCtx, 32);
                    if (pb != IntPtr.Zero) FF.avio_closep(ref pb);
                    FF.avformat_free_context(outCtx);
                }
                if (inCtx != IntPtr.Zero) FF.avformat_close_input(ref inCtx);
            }
        }

        // ─────────────────────────────────────────────────────────
        //  辅助
        // ─────────────────────────────────────────────────────────

        private static long RescaleTs(long ts, AVRational srcTb, AVRational dstTb)
        {
            return FF.av_rescale_q(ts, srcTb, dstTb);
        }

        private static string GetCodecName(int codecId)
        {
            // 常见编解码器 ID（AVCodecID）→ 名称
            return codecId switch
            {
                27  => "H.264",
                173 => "H.265/HEVC",
                167 => "VP9",
                139 => "VP8",
                86018 => "AAC",
                86017 => "MP3",
                86020 => "AC3",
                86022 => "EAC3",
                86021 => "DTS",
                1     => "MPEG1VIDEO",
                2     => "MPEG2VIDEO",
                _     => $"codec#{codecId}"
            };
        }

        private static string FormatFileSize(long bytes)
        {
            if (bytes >= 1024L * 1024 * 1024) return $"{bytes / (1024.0 * 1024 * 1024):F2} GB";
            if (bytes >= 1024 * 1024)          return $"{bytes / (1024.0 * 1024):F1} MB";
            return $"{bytes / 1024.0:F0} KB";
        }
    }
}

