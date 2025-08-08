using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace ResourceChecker
{
    internal class ImageDecodeRule : Rule
    {
        public override string Name => "[hidden]图片解码";

        public override string Description => "图片解码";

        public override string Filter
        {
            get => _filter;
            set => _filter = value;
        }
        private string _filter = "jpg|png|tga|tiff";


        public override Type InputType => typeof(byte[]);
        public override Type OutputType => typeof(System.Drawing.Color[]);

        public override object Resolve(byte[] data)
        {
            throw new NotImplementedException();
        }
        //public void DoCheck()
        //{
        //    throw new NotImplementedException();
        //}
    }
}
