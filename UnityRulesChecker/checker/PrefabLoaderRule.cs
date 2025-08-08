using System;
using System.IO;

namespace ResourceChecker
{
    class PrefabLoaderRule : Rule
    {
        public override string Name => "[hidden]Prefab加载器";

        public override string Description => "实现prefab加载功能，便于后续检查";

        public override string Filter
        {
            get => _filter;
            set => _filter = value;
        }
        private string _filter = "prefab";

        public override Type InputType => typeof(FileInfo);
        public override Type OutputType => typeof(UnityEngine.GameObject);

        public override object Resolve(FileInfo fileInfo)
        {
            throw new NotImplementedException();
        }

        //public void DoCheck()
        //{
        //    throw new NotImplementedException();
        //}
    }
}
