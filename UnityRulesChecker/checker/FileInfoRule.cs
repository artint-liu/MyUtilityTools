using System;
using System.IO;

namespace ResourceChecker
{
    class FileInfoRule : Rule
    {
        public override string Name => "文件信息检查";
        public override string Description => "检查文件基本信息";
        //public override string Filter
        //{
        //    get => _filter;
        //    set => _filter = value;
        //}

        public override Type InputType => typeof(FileInfo);
        public override Type OutputType => typeof(FileInfo);

        //private string _filter;
        public override object Resolve(FileInfo fileInfo)
        {
            return fileInfo;
        }
        public void DoCheck()
        {
            throw new NotImplementedException();
        }
    }
}
