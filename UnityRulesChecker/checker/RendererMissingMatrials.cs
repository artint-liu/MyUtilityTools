using System;
using System.IO;

namespace ResourceChecker
{
    class RendererMissingMatrials : Rule
    {
        public override string Name => "MissingMaterial检查";

        public override string Description => "MissingMaterial检查";

        public override string Filter
        {
            get => _filter;
            set => _filter = value;
        }
        private string _filter = "prefab|unity";

        public override Type InputType => typeof(UnityEngine.Renderer[]);

        public void DoCheck()
        {
            throw new NotImplementedException();
        }
    }
}
