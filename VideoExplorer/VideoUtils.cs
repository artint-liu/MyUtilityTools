using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using FFmpeg.AutoGen;
using System.Text;
using System.Threading.Tasks;
using System.Runtime.InteropServices;

namespace VideoExplorer
{
    public unsafe class VideoUtils
    {

        public static string GenerateThumbnail(string videoPath, string outputThumbnailPath, int frame, int width, bool bForceRegenerate = false)
        {
            string outputThumbnailPathFormat = Utils.AddPostfix(outputThumbnailPath, "{0:D3}");
            string firstCaptureFilename = string.Format(outputThumbnailPathFormat, 1); // filename.001.jpg
            if (!bForceRegenerate && File.Exists(firstCaptureFilename))
                return firstCaptureFilename;

            //ffmpeg.avformat_network_init(); // 不建议使用了

            AVFormatContext* pFormatContext = null;
            try
            {
                // 打开输入文件
                AVInputFormat fmt;
                int ret = ffmpeg.avformat_open_input(&pFormatContext, videoPath, &fmt, null);
                if (ret != 0) throw new ApplicationException("Could not open file");

                // 获取流信息
                if (ffmpeg.avformat_find_stream_info(pFormatContext, null) < 0)
                    throw new ApplicationException("Could not find stream information");

                // 查找视频流
                int videoStreamIndex = FindVideoStream(pFormatContext);               
                if (videoStreamIndex == -1) throw new ApplicationException("No video stream found");

                // 准备解码器
                AVCodecParameters* codecParams = pFormatContext->streams[videoStreamIndex]->codecpar;
                AVCodec* codec = ffmpeg.avcodec_find_decoder(codecParams->codec_id);
                AVCodecContext* codecContext = ffmpeg.avcodec_alloc_context3(codec);
                ffmpeg.avcodec_parameters_to_context(codecContext, codecParams);
                if (ffmpeg.avcodec_open2(codecContext, codec, null) < 0)
                    throw new ApplicationException("Could not open codec");

                // 计算总时长
                AVStream* videoStream = pFormatContext->streams[videoStreamIndex];
                double duration = videoStream->duration * ffmpeg.av_q2d(videoStream->time_base);
                if (duration <= 0) duration = pFormatContext->duration / (double)ffmpeg.AV_TIME_BASE;

                // 生成时间点
                var timePoints = new List<double>();
                for (int i = 1; i <= frame; i++)
                    timePoints.Add(i * duration / (frame + 1));

                // 创建输出目录
                //Directory.CreateDirectory(outputDir);

                // 处理每个时间点
                for (int i = 0; i < timePoints.Count; i++)
                {
                    double targetTime = timePoints[i];
                    long targetTs = (long)(targetTime / ffmpeg.av_q2d(videoStream->time_base));

                    // 定位到关键帧
                    ffmpeg.av_seek_frame(pFormatContext, videoStreamIndex, targetTs, ffmpeg.AVSEEK_FLAG_BACKWARD);
                    ffmpeg.avcodec_flush_buffers(codecContext);

                    AVPacket* packet = ffmpeg.av_packet_alloc();
                    AVFrame* avframe = ffmpeg.av_frame_alloc();
                    bool frameFound = false;

                    while (ffmpeg.av_read_frame(pFormatContext, packet) >= 0)
                    {
                        if (packet->stream_index != videoStreamIndex)
                        {
                            ffmpeg.av_packet_unref(packet);
                            continue;
                        }

                        if (ffmpeg.avcodec_send_packet(codecContext, packet) < 0) break;

                        while (ffmpeg.avcodec_receive_frame(codecContext, avframe) == 0)
                        {
                            double frameTime = avframe->best_effort_timestamp * ffmpeg.av_q2d(videoStream->time_base);
                            if (frameTime >= targetTime)
                            {
                                SaveFrame(avframe, codecContext, width, string.Format(outputThumbnailPathFormat, i + 1));
                                frameFound = true;
                                break;
                            }
                        }

                        ffmpeg.av_packet_unref(packet);
                        if (frameFound) break;
                    }

                    ffmpeg.av_packet_free(&packet);
                    ffmpeg.av_frame_free(&avframe);
                }

                // 清理资源
                ffmpeg.avcodec_free_context(&codecContext);
                ffmpeg.avformat_close_input(&pFormatContext);
            }
            finally
            {
                if (pFormatContext != null) ffmpeg.avformat_close_input(&pFormatContext);
            }
            return firstCaptureFilename;
        }

        private static int FindVideoStream(AVFormatContext* pFormatContext)
        {
            for (int i = 0; i < pFormatContext->nb_streams; i++)
            {
                if (pFormatContext->streams[i]->codecpar->codec_type == AVMediaType.AVMEDIA_TYPE_VIDEO)
                {
                    return i;
                }
            }
            return -1;
        }

        private static unsafe void SaveFrame(AVFrame* frame, AVCodecContext* codecContext, int width, string outputPath)
        {
            // 修改目标像素格式为YUV420P（JPEG编码器支持格式）
            const AVPixelFormat TARGET_PIX_FMT = AVPixelFormat.AV_PIX_FMT_YUVJ420P;

            // 计算缩放尺寸（保持原有逻辑）
            int srcWidth = codecContext->width;
            int srcHeight = codecContext->height;
            int dstWidth = srcWidth > 512 ? 512 : srcWidth;
            int dstHeight = (int)(srcHeight * ((double)dstWidth / srcWidth));
            dstHeight = dstHeight % 2 == 0 ? dstHeight : dstHeight - 1;

            // 创建转换上下文（修改目标像素格式）
            SwsContext* swsContext = ffmpeg.sws_getContext(
                srcWidth, srcHeight,
                codecContext->pix_fmt,
                dstWidth, dstHeight,
                TARGET_PIX_FMT,  // 修改为YUV格式
                ffmpeg.SWS_BILINEAR, null, null, null);

            // 分配转换后的帧
            AVFrame* yuvFrame = ffmpeg.av_frame_alloc();
            yuvFrame->format = (int)TARGET_PIX_FMT;
            yuvFrame->width = dstWidth;
            yuvFrame->height = dstHeight;
            ffmpeg.av_frame_get_buffer(yuvFrame, 0);

            // 执行格式转换
            ffmpeg.sws_scale(swsContext, frame->data, frame->linesize, 0, frame->height, yuvFrame->data, yuvFrame->linesize);

            // 查找JPEG编码器（确保编码器存在）
            AVCodec* jpegCodec = ffmpeg.avcodec_find_encoder(AVCodecID.AV_CODEC_ID_MJPEG);
            if (jpegCodec == null)
                throw new ApplicationException("JPEG codec not found");

            // 配置编码器上下文（修改像素格式设置）
            AVCodecContext* jpegContext = ffmpeg.avcodec_alloc_context3(jpegCodec);
            jpegContext->width = dstWidth;
            jpegContext->height = dstHeight;
            jpegContext->pix_fmt = TARGET_PIX_FMT;  // 设置为YUV格式
            jpegContext->time_base = new AVRational { num = 1, den = 1 };
            jpegContext->color_range = AVColorRange.AVCOL_RANGE_JPEG; // 重要：设置颜色范围

            // 设置编码参数
            AVDictionary* options = null;
            ffmpeg.av_dict_set(&options, "quality", "90", 0);

            // 打开编码器（添加更详细的错误处理）
            int openResult = ffmpeg.avcodec_open2(jpegContext, jpegCodec, &options);
            if (openResult < 0)
            {
                byte[] errorBuffer = new byte[1024];
                fixed (byte* errorBufferPtr = errorBuffer)
                {
                    ffmpeg.av_strerror(openResult, errorBufferPtr, (ulong)errorBuffer.Length);
                    throw new ApplicationException($"Could not open JPEG codec: { System.Text.Encoding.UTF8.GetString(errorBuffer)}");
                }
            }

            AVPacket* packet = ffmpeg.av_packet_alloc();
            if (ffmpeg.avcodec_send_frame(jpegContext, yuvFrame) == 0)
            {
                if (ffmpeg.avcodec_receive_packet(jpegContext, packet) == 0)
                {
                    using (var fs = File.Create(outputPath))
                    {
                        byte[] data = new byte[packet->size];
                        Marshal.Copy((IntPtr)packet->data, data, 0, packet->size);
                        fs.Write(data, 0, data.Length);
                    }
                }
            }

            // 清理资源
            ffmpeg.av_packet_unref(packet);
            ffmpeg.av_packet_free(&packet);
            ffmpeg.avcodec_free_context(&jpegContext);
            ffmpeg.av_frame_free(&yuvFrame);
            ffmpeg.sws_freeContext(swsContext);


        }

    }
}
