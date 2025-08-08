using System;
using System.IO;

namespace ResourceChecker
{
    class GameObjectMissingScripts : Rule
    {
        public override string Name => "MissingScripts检查";

        public override string Description => "MissingScripts检查";

        public override string Filter
        {
            get => _filter;
            set => _filter = value;
        }
        private string _filter = "prefab|fbx";

        public override Type InputType => typeof(UnityEngine.GameObject);
        public void DoCheck()
        {
            throw new NotImplementedException();
        }
    }
}
