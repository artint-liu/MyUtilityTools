using System;
using System.Collections.Generic;
using UnityEngine;

namespace ResourceChecker
{
    class MeshesGeneratorRule : Rule
    {
        public override string Name => "Mesh收集器";

        public override string Description => "从GameObject中收集Mesh，进行后续检查";

        public override string Filter
        {
            get => _filter;
            set => _filter = value;
        }
        private string _filter = "prefab|unity|fbx";

        public override Type InputType => typeof(UnityEngine.GameObject);
        public override Type OutputType => typeof(UnityEngine.Mesh[]);

        public override object Evaluate(object input)
        {
            if(input is UnityEngine.GameObject go)
            {
                List<UnityEngine.Mesh> meshList = new();
                Renderer[] renderers = go.GetComponentsInChildren<UnityEngine.Renderer>();
                foreach (Renderer renderer in renderers)
                {
                    if(renderer is UnityEngine.MeshRenderer meshRenderer)
                    {
                        if(renderer.TryGetComponent<MeshFilter>(out MeshFilter meshFilter))
                        {
                            Mesh mesh = meshFilter.sharedMesh;
                            if(mesh)
                                meshList.Add(mesh);
                        }
                    }
                    else if (renderer is UnityEngine.SkinnedMeshRenderer skinnedMeshRenderer)
                    {
                        Mesh mesh = skinnedMeshRenderer.sharedMesh;
                        if (mesh)
                            meshList.Add(mesh);
                    }
                }
                return meshList.Count > 0 ? meshList.ToArray() : null;
            }
            return null;
        }
    }
}
