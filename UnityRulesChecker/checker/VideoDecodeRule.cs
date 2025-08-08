//using System;
//using System.IO;

//namespace ResourceChecker
//{
//    class VideoFormatRule : Rule
//    {
//        public override string Name => "[hidden]视频解码";
//        public override string Description => "视频解码规则";
//        public override string Filter
//        {
//            get => _filter;
//            set => _filter = value;
//        }
//        private string _filter = "mp4";

//        struct VideoFormat
//        {
//            int width;
//            int height;
//            int rate
//        }
//        public override Type InputType => typeof(FileInfo);
//        public override Type OutputType => typeof(void);

//        public override void Resolve(FileInfo fileInfo)
//        {
            
//        }
//        public void DoCheck()
//        {
//            throw new NotImplementedException();
//        }
//    }
//}
