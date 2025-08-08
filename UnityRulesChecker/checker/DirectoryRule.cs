using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace ResourceChecker
{
    class DirectoryRule : Rule
    {
        public override string Name => "目录规则";
        public override string Description => "目录规则检查";
        //public override string Filter
        //{
        //    get => _filter;
        //    set => _filter = value;
        //}
        public string wishList = "";
        public string whiteList = "";
        public string blackList = "";

        public class DirectoryDescription
        {
            public string[] dirs;
            public string[] files;

            public DirectoryDescription()
            {
            }

            public DirectoryDescription(string dirname)
            {
                files = Directory.GetFiles(dirname);
                dirs = Directory.GetDirectories(dirname);
            }
        }

        public override Type InputType => typeof(string);
        public override Type OutputType => typeof(List<string>);

        //private string _filter;

        //public override void Resolve(List<string> filenames)
        public override object Resolve(string dirname)
        {
            return new DirectoryDescription(dirname);
        }
        public void DoCheck()
        {
            
        }
    }
}
