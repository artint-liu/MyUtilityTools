using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace ResourceChecker
{
    internal class InvalidAlphaChannelRule : Rule
    {
        public override string Name => "无效Alpha通道";

        public override string Description => "检查文件是否包含无效alpha通道，如果全部像素的Alpha都大于阈值则认为Alpha通道没必要使用";

        public override string Filter
        {
            get => _filter;
            set => _filter = value;
        }
        private string _filter = "png|tga";

        public override Type InputType => typeof(System.Drawing.Color[]);

        public float factor = 0.95f;

        void DoCheck()
        {
            throw new NotImplementedException();
        }
    }
}
