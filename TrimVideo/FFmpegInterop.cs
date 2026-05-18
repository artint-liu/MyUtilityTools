using System;
using System.IO;
using System.Runtime.InteropServices;

namespace TrimVideo
{
    internal static class FF
    {
        internal const string AvFormat    = "avformat-62";
        internal const string AvCodec     = "avcodec-62";
        internal const string AvUtil      = "avutil-60";
        internal const string SwScale     = "swscale-9";

        internal const int AV_PIX_FMT_BGRA      = 28;
        internal const int AVMEDIA_TYPE_VIDEO    = 0;
        internal const int AVMEDIA_TYPE_AUDIO    = 1;
        internal const int SWS_FAST_BILINEAR     = 1;
        internal const long AV_NOPTS_VALUE       = unchecked((long)0x8000000000000000L);
        internal const int AV_TIME_BASE          = 1000000;
        internal const int AVIO_FLAG_WRITE       = 2;

        // avformat — url 用 LPUTF8Str 支持中文路径
        [DllImport(AvFormat, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void avformat_network_init();
        [DllImport(AvFormat, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int avformat_open_input(ref IntPtr ps, [MarshalAs(UnmanagedType.LPUTF8Str)] string url, IntPtr fmt, ref IntPtr options);
        [DllImport(AvFormat, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int avformat_find_stream_info(IntPtr ic, IntPtr options);
        [DllImport(AvFormat, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void avformat_close_input(ref IntPtr s);
        [DllImport(AvFormat, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int av_read_frame(IntPtr s, IntPtr pkt);
        [DllImport(AvFormat, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int avformat_seek_file(IntPtr s, int stream_index, long min_ts, long ts, long max_ts, int flags);
        [DllImport(AvFormat, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int avformat_alloc_output_context2(ref IntPtr ctx, IntPtr oformat, [MarshalAs(UnmanagedType.LPUTF8Str)] string? format_name, [MarshalAs(UnmanagedType.LPUTF8Str)] string? filename);
        [DllImport(AvFormat, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr avformat_new_stream(IntPtr s, IntPtr c);
        [DllImport(AvFormat, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int avio_open(ref IntPtr s, [MarshalAs(UnmanagedType.LPUTF8Str)] string url, int flags);
        [DllImport(AvFormat, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int avio_closep(ref IntPtr s);
        [DllImport(AvFormat, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void avformat_free_context(IntPtr s);
        [DllImport(AvFormat, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int av_find_best_stream(IntPtr ic, int type, int wanted, int related, ref IntPtr dec_ret, int flags);
        [DllImport(AvFormat, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int avformat_write_header(IntPtr s, ref IntPtr options);
        [DllImport(AvFormat, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int av_interleaved_write_frame(IntPtr s, IntPtr pkt);
        [DllImport(AvFormat, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int av_write_trailer(IntPtr s);

        // avcodec
        [DllImport(AvCodec, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr avcodec_find_decoder(int id);
        [DllImport(AvCodec, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr avcodec_alloc_context3(IntPtr codec);
        [DllImport(AvCodec, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int avcodec_parameters_to_context(IntPtr codec, IntPtr par);
        [DllImport(AvCodec, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int avcodec_open2(IntPtr avctx, IntPtr codec, ref IntPtr options);
        [DllImport(AvCodec, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void avcodec_free_context(ref IntPtr avctx);
        [DllImport(AvCodec, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int avcodec_send_packet(IntPtr avctx, IntPtr avpkt);
        [DllImport(AvCodec, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int avcodec_receive_frame(IntPtr avctx, IntPtr frame);
        [DllImport(AvCodec, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void avcodec_flush_buffers(IntPtr avctx);
        [DllImport(AvCodec, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr av_packet_alloc();
        [DllImport(AvCodec, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void av_packet_free(ref IntPtr pkt);
        [DllImport(AvCodec, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void av_packet_unref(IntPtr pkt);
        [DllImport(AvCodec, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int av_packet_ref(IntPtr dst, IntPtr src);
        [DllImport(AvCodec, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int avcodec_parameters_copy(IntPtr dst, IntPtr src);

        // avutil
        [DllImport(AvUtil, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr av_frame_alloc();
        [DllImport(AvUtil, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void av_frame_free(ref IntPtr frame);
        [DllImport(AvUtil, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void av_frame_unref(IntPtr frame);
        [DllImport(AvUtil, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void av_log_set_level(int level);
        [DllImport(AvUtil, CallingConvention = CallingConvention.Cdecl)]
        internal static extern long av_rescale_q(long a, AVRational bq, AVRational cq);

        // swscale
        [DllImport(SwScale, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr sws_getContext(int srcW, int srcH, int srcFormat, int dstW, int dstH, int dstFormat, int flags, IntPtr srcFilter, IntPtr dstFilter, IntPtr param);
        [DllImport(SwScale, CallingConvention = CallingConvention.Cdecl)]
        internal static extern unsafe int sws_scale(IntPtr c, byte** srcSlice, int* srcStride, int srcSliceY, int srcSliceH, byte** dst, int* dstStride);
        [DllImport(SwScale, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void sws_freeContext(IntPtr swsContext);
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct AVRational
    {
        public int num;
        public int den;
        public double ToDouble() => den != 0 ? (double)num / den : 0;
        public static readonly AVRational AV_TIME_BASE_Q = new AVRational { num = 1, den = 1000000 };
    }

    // ── 字段偏移（已通过运行时诊断确认，FFmpeg 8.1.1 x64）─────────────────────────

    /// <summary>AVFormatContext 字段偏移</summary>
    internal static class FmtCtx
    {
        // nb_streams @ 44,  streams @ 48,  duration @ 104,  pb @ 32
        public static int    NbStreams(IntPtr p) => Marshal.ReadInt32(p, 44);
        public static IntPtr StreamsArr(IntPtr p) => Marshal.ReadIntPtr(p, 48);
        public static long   Duration(IntPtr p) => Marshal.ReadInt64(p, 104);
        public static IntPtr Stream(IntPtr p, int i) => Marshal.ReadIntPtr(StreamsArr(p), i * IntPtr.Size);
        public static IntPtr Pb(IntPtr p)        => Marshal.ReadIntPtr(p, 32);
        public static void   SetPb(IntPtr p, IntPtr pb) => Marshal.WriteIntPtr(p, 32, pb);
    }

    /// <summary>AVStream 字段偏移（FFmpeg 8.1.1 x64 实测）</summary>
    internal static class StreamCtx
    {
        // codecpar      @ 16 (ptr)
        // time_base     @ 32 (AVRational)
        // duration      @ 48 (int64)
        // nb_frames     @ 56 (int64)
        // avg_frame_rate @ 88 (AVRational)
        public static IntPtr  CodecPar(IntPtr s) => Marshal.ReadIntPtr(s, 16);
        public static AVRational TimeBase(IntPtr s) => new AVRational {
            num = Marshal.ReadInt32(s, 32), den = Marshal.ReadInt32(s, 36)
        };
        public static long    Duration(IntPtr s) => Marshal.ReadInt64(s, 48);
        public static AVRational AvgFrameRate(IntPtr s) => new AVRational {
            num = Marshal.ReadInt32(s, 88), den = Marshal.ReadInt32(s, 92)
        };
    }

    /// <summary>AVCodecParameters 字段偏移（FFmpeg 8.1.1 x64 实测）</summary>
    internal static class CodecParCtx
    {
        // codec_type @ 0,  codec_id @ 4,  width @ 72,  height @ 76
        public static int CodecType(IntPtr p) => Marshal.ReadInt32(p, 0);
        public static int CodecId(IntPtr p)   => Marshal.ReadInt32(p, 4);
        public static int Width(IntPtr p)     => Marshal.ReadInt32(p, 72);
        public static int Height(IntPtr p)    => Marshal.ReadInt32(p, 76);
    }

    /// <summary>AVFrame 字段偏移（FFmpeg 8.1.1 x64）</summary>
    /// <remarks>
    /// data[8]:              0   (8 ptrs × 8 = 64)
    /// linesize[8]:         64   (8 ints × 4 = 32)
    /// extended_data:       96   (ptr, 8)
    /// width:              104   (int, 4)
    /// height:             108   (int, 4)
    /// nb_samples:         112   (int, 4)
    /// format:             116   (int, 4)
    /// pict_type:          120   (enum/int, 4)
    /// [pad 4 bytes]
    /// sample_aspect_ratio:128   (AVRational, 8)  ← 实际对齐到 4，但后续 pts 需 8 对齐
    /// pts:                136   (int64, 8)  ← 需要 8 字节对齐，所以跳过 132→136
    /// pkt_dts:            144   (int64, 8)
    /// </remarks>
    internal static class FrameCtx
    {
        public static IntPtr Data(IntPtr f, int plane) => Marshal.ReadIntPtr(f, plane * 8);
        public static int    LineSize(IntPtr f, int plane) => Marshal.ReadInt32(f, 64 + plane * 4);
        public static int    Width(IntPtr f)  => Marshal.ReadInt32(f, 104);
        public static int    Height(IntPtr f) => Marshal.ReadInt32(f, 108);
        public static int    Format(IntPtr f) => Marshal.ReadInt32(f, 116);
        public static long   Pts(IntPtr f)    => Marshal.ReadInt64(f, 136);
    }

    /// <summary>AVPacket 字段偏移（FFmpeg 8.x x64）</summary>
    internal static class PktCtx
    {
        public static long GetPts(IntPtr p)  => Marshal.ReadInt64(p, 8);
        public static long GetDts(IntPtr p)  => Marshal.ReadInt64(p, 16);
        public static int  GetSize(IntPtr p) => Marshal.ReadInt32(p, 32);
        public static int  GetStreamIndex(IntPtr p) => Marshal.ReadInt32(p, 36);
        public static void SetPts(IntPtr p, long v)  => Marshal.WriteInt64(p, 8, v);
        public static void SetDts(IntPtr p, long v)  => Marshal.WriteInt64(p, 16, v);
        public static void SetStreamIndex(IntPtr p, int v) => Marshal.WriteInt32(p, 36, v);
        public static void SetDuration(IntPtr p, long v) => Marshal.WriteInt64(p, 48, v);
    }

    // ── DLL 预加载（必须按依赖顺序）────────────────────────────────────────

    internal static class NativeLibraryLoader
    {
        private static bool _loaded;

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern bool SetDllDirectory(string lpPathName);

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern IntPtr LoadLibrary(string lpFileName);

        public static void EnsureLoaded(string dir)
        {
            if (_loaded) return;
            SetDllDirectory(dir);
            // avformat-62 依赖其他库，必须先逐一显式加载
            string[] order = { "avutil-60.dll", "swresample-6.dll", "swscale-9.dll", "avcodec-62.dll", "avformat-62.dll" };
            foreach (var dll in order)
                LoadLibrary(Path.Combine(dir, dll));
            _loaded = true;
        }
    }
}
