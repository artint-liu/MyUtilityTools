using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Text.RegularExpressions;
using UnityEngine;

namespace ResourceChecker
{
    public class HeadBoundaryRule : Rule
    {
        public override string Name => "$头部模型边缘";
        public override string Description => "加载头部模型及收集边缘数据，用于后续检查头身接缝";
        public override Type InputType => typeof(GameObject);
        public override Type OutputType => typeof(BoundaryDescription);

        public string NameRegex;

        private static Bounds femaleBounds = new Bounds(new Vector3(0, 1.484126f, -0.05934739f), new Vector3(0.06776363f, 0.007535841f, 0.06825212f));
        private static Bounds maleBounds = new Bounds(new Vector3(0, 1.585638f, -0.03893582f), new Vector3(0.08488183f, 0.01588907f, 0.07983167f));


        public struct VERTEX
        {
            public Vector3 position;
            public Vector3 normal;
            public BoneWeight boneWeight;
        }

        public class BoundaryDescription
        {
            public SkinnedMeshRenderer headRenderer;
            public List<VERTEX> boundary;
        }

        public override object Evaluate(object input)
        {
            GameObject go = input as GameObject;
            var smrs = go.GetComponentsInChildren<SkinnedMeshRenderer>();
            BoundaryDescription boundaryDescription = new BoundaryDescription();
            foreach (var smr in smrs)
            {
                if (smr.sharedMesh && Regex.IsMatch(smr.gameObject.name, NameRegex))
                {
                    boundaryDescription.headRenderer = smr;
                    boundaryDescription.boundary = GenerateBoundaryVertexEx(smr.sharedMesh, smr.gameObject.name.ToUpper().StartsWith("SK_M_"));
                    return boundaryDescription;
                }
            }
            return null;
        }

        public static HashSet<Vector3> GenerateBoundaryVertex(Mesh mesh, bool male) // 收集模型边缘顶点
        {
            var vertices = mesh.vertices;
            var indices = mesh.triangles;

            Dictionary<(Vector3, Vector3), int> edgeCount = new Dictionary<(Vector3, Vector3), int>();
            int count = indices.Length;
            for (var i = 0; i < count; i += 3)
            {
                for (int n = 0; n < 3; n++)
                {
                    var pair1 = (n == 2)
                        ? (vertices[indices[i + 2]], vertices[indices[i]])
                        : (vertices[indices[i + n]], vertices[indices[i + n + 1]]);
                    var pair2 = (pair1.Item2, pair1.Item1);


                    if (edgeCount.ContainsKey(pair1))
                    {
                        edgeCount[pair1]++;
                    }
                    else if (edgeCount.ContainsKey(pair2))
                    {
                        edgeCount[pair2]++;
                    }
                    else
                    {
                        edgeCount.Add(pair1, 1);
                    }
                }
            }

            HashSet<Vector3> result = new HashSet<Vector3>();
            Bounds bounds = male ? maleBounds : femaleBounds;
            foreach (var pair in edgeCount)
            {
                if (pair.Value == 1)
                {
                    if(bounds.Contains(pair.Key.Item1))
                        result.Add(pair.Key.Item1);
                    if(bounds.Contains(pair.Key.Item2))
                        result.Add(pair.Key.Item2);
                }
            }

            return result;
        }

        public static List<VERTEX> GenerateBoundaryVertexEx(Mesh mesh, bool male) // 收集模型边缘顶点，和对应的法线，骨骼权重
        {
            HashSet<Vector3> hashSet = GenerateBoundaryVertex(mesh, male);
            if (hashSet.Count == 0)
                return null;

            List<VERTEX> result = new();
            Vector3[] vertices = mesh.vertices;
            Vector3[] normals = mesh.normals;
            BoneWeight[] boneWeights = mesh.boneWeights;

            int count = vertices.Length;
            for(int i = 0; i < count; i++)
            {
                if(hashSet.Contains(vertices[i]))
                {
                    result.Add(new VERTEX {
                        position = vertices[i],
                        normal = normals.Length > 0 ? normals[i] : default,
                        boneWeight = boneWeights.Length > 0 ? boneWeights[i] : default
                    });
                }
            }
            return result;
        }

    }
}
