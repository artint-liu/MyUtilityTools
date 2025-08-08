using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace ResourceChecker
{
    internal class SolidColorImage : Rule
    {
        public override string Name => "纯色图片";

        public override string Description => "检查纯色图片大小，防止超大尺寸单一颜色图片";

        public override string Filter
        {
            get => _filter;
            set => _filter = value;
        }
        private string _filter = "png|tga|jpg|jpeg";

        public int sizeLimit = 32;

        public override Type InputType => typeof(System.Drawing.Color[]);


        public void DoCheck()
        {
            throw new NotImplementedException();
        }
    }
}
