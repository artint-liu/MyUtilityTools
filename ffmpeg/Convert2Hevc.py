import subprocess
import json
import os
import argparse
import platform

def get_video_info(input_file):
    # 使用 ffprobe 获取视频信息
    cmd = [
        'ffprobe',
        '-v', 'quiet',
        '-print_format', 'json',
        '-show_streams',
        input_file
    ]
    
    result = subprocess.run(cmd, capture_output=True)
    info = json.loads(result.stdout)
    
    # 提取视频流信息
    video_stream = None
    for stream in info['streams']:
        if stream['codec_type'] == 'video':
            video_stream = stream
            break
    
    return video_stream

def convert_video(input_file, output_file):
    # 获取输入视频信息
    video_info = get_video_info(input_file)
    codec_name = video_info.get('codec_name', '').lower()
    bit_rate = int(video_info.get('bit_rate', 0))
    
    # 根据格式调整码率
    if codec_name == 'hevc':
        target_bitrate = bit_rate
    else:
        target_bitrate = bit_rate // 2
    
    # 如果比特率为 0，设置默认码率
    if target_bitrate == 0:
        target_bitrate = 4000000
    
    # 转换为 QuickTime 支持的格式（H.264 或 HEVC）
    # if codec_name != 'hevc':
    #     output_codec = 'libx264'
    # else:
    #     output_codec = 'libx265'  # 如果输入是 HEVC，保持 HEVC 格式

    os_name = platform.system()
    if os_name == 'Darwin':
        output_codec = 'libx265'
        # output_codec = 'hevc_videotoolbox'
    elif os_name == 'Windows':
        output_codec = 'hevc_qsv'

    # output_codec = 'hevc_videotoolbox'
    
    print("输入文件:" + input_file)
    print("输入文件比特率:%d\n"%(bit_rate))
    print("输出文件:" + output_file)
    print("输出文件比特率:%d\n"%(target_bitrate))

    # 组建 FFmpeg 命令
    cmd = [
        'ffmpeg',
        '-i', input_file,
        '-vcodec', output_codec,
        # '-crf', '23',
        '-b:v', f'{target_bitrate}',
        '-tag:v', 'hvc1',
        '-maxrate', f'{target_bitrate}',
        '-c:a', 'copy',
        '-y',  # 覆盖输出文件
        output_file
    ]
    
    subprocess.run(cmd, check=True)

if __name__ == "__main__":
    # 解析命令行参数
    parser = argparse.ArgumentParser(description='Convert video to HEVC format')
    parser.add_argument('input_file', help='Input video file')
    args = parser.parse_args()
    
    input_path = args.input_file
    output_path = f"{os.path.splitext(input_path)[0]}_HEVC{os.path.splitext(input_path)[1]}"
    
    # 检查输入文件是否存在
    if not os.path.exists(input_path):
        print(f"Error: Input file '{input_path}' does not exist.")
        exit(1)
    
    try:
        convert_video(input_path, output_path)
        print(f"Conversion completed. Output file saved as '{output_path}'.")
    except Exception as e:
        print(f"An error occurred during conversion: {str(e)}")
