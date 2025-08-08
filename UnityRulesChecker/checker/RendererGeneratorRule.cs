using System;
using System.IO;

namespace ResourceChecker
{
    class RendererGeneratorRule : Rule
    {
        public override string Name => "Renderer收集器";

        public override string Description => "从GameObject中收集Renderer";

        public override string Filter
        {
            get => _filter;
            set => _filter = value;
        }
        private string _filter = "prefab|unity";

        public override Type InputType => typeof(UnityEngine.GameObject);
        public override Type OutputType => typeof(UnityEngine.Renderer[]);

        public void DoCheck()
        {
            throw new NotImplementedException();
        }
    }
}
