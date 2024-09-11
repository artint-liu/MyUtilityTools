#define _CRT_SECURE_NO_WARNINGS
/*
 * Video Acceleration API (video transcoding) transcode sample
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file
 * Intel VAAPI-accelerated transcoding example.
 *
 * @example vaapi_transcode.c
 * This example shows how to do VAAPI-accelerated transcoding.
 * Usage: vaapi_transcode input_stream codec output_stream
 * e.g: - vaapi_transcode input.mp4 h264_vaapi output_h264.mp4
 *      - vaapi_transcode input.mp4 vp9_vaapi output_vp9.ivf
 */

#include <stdio.h>
#include <errno.h>
#include <clstd.h>
#include <clstring.h>
#include <clPathFile.h>
extern "C"
{
  #include <libavutil/hwcontext.h>
  #include <libavcodec/avcodec.h>
  #include <libavformat/avformat.h>
}

//, *ofmt_ctx = NULL;
//static AVBufferRef *hw_device_ctx = NULL;
static int initialized = 0;
FILE* fp;

static enum AVPixelFormat get_vaapi_format(AVCodecContext *ctx,
  const enum AVPixelFormat *pix_fmts)
{
  const enum AVPixelFormat *p;

  for(p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
    if(*p == AV_PIX_FMT_VAAPI)
      return *p;
  }

  fprintf(stderr, "Unable to decode this file using VA-API.\n");
  return AV_PIX_FMT_NONE;
}

class FFMpeg
{
  AVFormatContext *ifmt_ctx = NULL;
  AVCodecContext *decoder_ctx = NULL, *encoder_ctx = NULL;
  int video_stream = -1;
  AVStream *ost;

public:
  virtual ~FFMpeg()
  {
    avformat_close_input(&ifmt_ctx);
    avcodec_free_context(&decoder_ctx);
    avcodec_free_context(&encoder_ctx);
  }

  int Open(const char *filename)
  {
    int ret;
    const AVCodec *decoder = NULL;
    AVStream *video = NULL;

    if((ret = avformat_open_input(&ifmt_ctx, filename, NULL, NULL)) < 0) {
      fprintf(stderr, "Cannot open input file '%s', Error code: %d\n",
        filename, (ret));
      return ret;
    }

    if((ret = avformat_find_stream_info(ifmt_ctx, NULL)) < 0) {
      fprintf(stderr, "Cannot find input stream information. Error code: %d\n",
        (ret));
      return ret;
    }

    ret = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    video_stream = ret;

    if(!(decoder_ctx = avcodec_alloc_context3(decoder)))
      return AVERROR(ENOMEM);

    video = ifmt_ctx->streams[video_stream];
    if((ret = avcodec_parameters_to_context(decoder_ctx, video->codecpar)) < 0) {
      fprintf(stderr, "avcodec_parameters_to_context error. Error code: %d\n",
        (ret));
      return ret;
    }

    //decoder_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
    //if (!decoder_ctx->hw_device_ctx) {
    //    fprintf(stderr, "A hardware device reference create failed.\n");
    //    return AVERROR(ENOMEM);
    //}
    decoder_ctx->get_format = get_vaapi_format;

    if((ret = avcodec_open2(decoder_ctx, decoder, NULL)) < 0) {
      fprintf(stderr, "Failed to open codec for decoding. Error code: %d\n", (ret));
    }
    return ret;
  }

  const AVFormatContext* GetFormatContext() const
  {
    return ifmt_ctx;
  }

  const AVCodecContext* GetDecoder() const
  {
    return decoder_ctx;
  }
};


static int open_input_file(const char *filename)
{
    int ret;

    FFMpeg ff;
    ret = ff.Open(filename);
    if(ret < 0) {
      return ret;
    }

    clStringA strInputFilename = filename;
    strInputFilename.MakeUpper();
    b32 bIsWMV = clpathfile::CompareExtension(strInputFilename, "WMV");

    //fprintf(fp, "call tohevc \"%s\" %dk\r\n", filename, int(ifmt_ctx->bit_rate / 2 / 1024));
    clStringA strHEVC = filename;
    clpathfile::RenameExtension(strHEVC, ".hevc.mp4");
    if(clpathfile::IsPathExist(strHEVC))
    {
      FFMpeg hevc;
      if(hevc.Open(strHEVC) >= 0)
      {
        auto hevc_dec = hevc.GetDecoder();
        auto src_dec = ff.GetDecoder();
        if(hevc_dec->width == src_dec->width && hevc_dec->height == src_dec->height)
        {
          fprintf(fp, "del \"%s\"\r\n", filename);
          return ret;
        }
      }
    }
    int bit_rate = int(ff.GetFormatContext()->bit_rate / 2 / 1024);
    fprintf(fp, "ffmpeg -i \"%s\" -vcodec %%ENCODER%% -b:v %dk -acodec %s \"%s\"\r\n", filename, bit_rate, bIsWMV ? "aac" : "copy", strHEVC.CStr());
    if (ret < 0) {
        fprintf(stderr, "Cannot find a video stream in the input file. "
                "Error code: %d\n", (ret));
        return ret;
    }

    return ret;
}

int main(int argc, char **argv)
{
    int ret = 0;

    fp = fopen("trans_hevc.cmd", "wb");

    fprintf(fp, "rem 码率降低一半\r\n");
    fprintf(fp, "set ENCODER=hevc_nvenc\r\n");
    fprintf(fp, "rem hevc_qsv is \"Intel Quick Sync Video acceleration\"\r\n");
    fprintf(fp, "rem set ENCODER=hevc_qsv\r\n");

    cllist<clStringA> files;
    for(int i = 1; i < argc; i++)
    {
      clpathfile::GenerateFiles(files, argv[i], [](const clStringA& strDir, const clstd::FINDFILEDATAA& data) -> b32
      {
        if(data.dwAttributes & clstd::FileAttribute_Directory) {
          return TRUE;
        }

        clStringA strFile = data.cFileName;
        strFile.MakeUpper();
        if(clpathfile::CompareExtension(strFile, "MP4|MKV|AVI|MOV|RM|RMVB|WMV") && strFile.Find(".HEVC.") == clStringA::npos)
        {
          return TRUE;
        }
        return FALSE;
      });
    }

    for(clStringA& strFile : files)
    {
      CLOG(strFile);
      ret = open_input_file(strFile);
      //avformat_close_input(&ifmt_ctx);
      //avcodec_free_context(&decoder_ctx);
      //avcodec_free_context(&encoder_ctx);
    }

    fclose(fp);

    printf("按任意键退出...");
    clstd_cli::getch();

    //if (!(enc_codec = avcodec_find_encoder_by_name(argv[2]))) {
    //    fprintf(stderr, "Could not find encoder '%s'\n", argv[2]);
    //    ret = -1;
    //    goto end;
    //}

    //if ((ret = (avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, argv[3]))) < 0) {
    //    fprintf(stderr, "Failed to deduce output format from file extension. Error code: "
    //            "%d\n", (ret));
    //    goto end;
    //}

    //if (!(encoder_ctx = avcodec_alloc_context3(enc_codec))) {
    //    ret = AVERROR(ENOMEM);
    //    goto end;
    //}

    //ret = avio_open(&ofmt_ctx->pb, argv[3], AVIO_FLAG_WRITE);
    //if (ret < 0) {
    //    fprintf(stderr, "Cannot open output file. "
    //            "Error code: %d\n", (ret));
    //    goto end;
    //}

    ///* read all packets and only transcoding video */
    //while (ret >= 0) {
    //    if ((ret = av_read_frame(ifmt_ctx, &dec_pkt)) < 0)
    //        break;

    //    if (video_stream == dec_pkt.stream_index)
    //        ret = dec_enc(&dec_pkt, enc_codec);

    //    av_packet_unref(&dec_pkt);
    //}

    ///* flush decoder */
    //dec_pkt.data = NULL;
    //dec_pkt.size = 0;
    //ret = dec_enc(&dec_pkt, enc_codec);
    //av_packet_unref(&dec_pkt);

    ///* flush encoder */
    //ret = encode_write(NULL);

    /* write the trailer for output stream */
    //av_write_trailer(ofmt_ctx);

//end:
    //avformat_close_input(&ifmt_ctx);
    ////avformat_close_input(&ofmt_ctx);
    //avcodec_free_context(&decoder_ctx);
    //avcodec_free_context(&encoder_ctx);
    //av_buffer_unref(&hw_device_ctx);
    return ret;
}
