using System;

namespace ResourceChecker
{
    internal class UTF8WishBOMRule : Rule
    {
        public override string Name => "UTF8包含BOM";

        public override string Description => "UTF8格式格式必须包含BOM";

        public override string Filter
        {
            get => _filter;
            set => _filter = value;
        }
        private string _filter = "txt";
        public override Type InputType => typeof(byte[]);

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
