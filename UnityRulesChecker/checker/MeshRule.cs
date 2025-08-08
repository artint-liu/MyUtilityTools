using System;
using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering;

namespace ResourceChecker
{
    public class MeshRule : Rule
    {
        public override string Name => "模型检查";

        public override string Description => "检查模型的基本参数是否符合要求";

        public override string Filter { get => _filter; set => _filter = value; }

        private string _filter = "prefab|fbx";

        public override Type InputType => typeof(Mesh[]);

        public int FaceLimit = 800;
        public int UVLimit = 1; // 只有一层UV

        public override object Evaluate(object input)
        {
            if (input is Mesh[] meshes)
            {
                foreach (Mesh mesh in meshes)
                {
                    int faceCount = mesh.triangles.Length / 3;
                    if (faceCount > FaceLimit)
                    {
                        Debug.LogError($"面数超标，限制值: {FaceLimit}, 实际面数:{faceCount}");
                    }

                    int uvcount = 0;
                    VertexAttributeDescriptor[] attrs = mesh.GetVertexAttributes();
                    foreach (VertexAttributeDescriptor attr in attrs)
                    {
                        if (attr.attribute >= VertexAttribute.TexCoord0 && attr.attribute <= VertexAttribute.TexCoord7)
                            uvcount++;
                    }

                    if (uvcount > UVLimit)
                        Debug.LogError("uv超标");
                }
            }
            return null;
        }
    }
}
