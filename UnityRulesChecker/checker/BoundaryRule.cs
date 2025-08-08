using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using UnityEngine;

namespace ResourceChecker
{
    public class BoundaryRule : Rule
    {
        public override string Name => "头身接缝检查";
        public override string Description => "检查身体接缝是否与头部重合";
        public override string Filter { get => _filter; set => _filter = value; }
        private string _filter = "fbx";
        public override Type InputType => typeof(GameObject);
        //public override Type ReferenceType => typeof(HeadBoundaryRule.BoundaryDescription);
        public override Rule ReferenceRule
        {
            get => _referenceRule;
            internal set => _referenceRule = value; // 加载器设置
        }

        private Rule _referenceRule = new HeadBoundaryRule();
        //public List<Vector3> boundary;

        private static string Show_SKM = "Assets/Art_Resources/Domestic/Dimension/Character/01_Male/P0000/02_Head/A/0000/Show/SK_M_P0000_Head_A0000_";
        private static string Show_SKF = "Assets/Art_Resources/Domestic/Dimension/Character/02_Female/P0000/02_Head/A/0000/Show/SK_F_P0000_Head_A0000_";

        private static string Battle_SKM = "Assets/Art_Resources/Domestic/Dimension/Character/01_Male/P0000/02_Head/A/0000/Battle/SK_M_P0000_Head_A0000_";
        private static string Battle_SKF = "Assets/Art_Resources/Domestic/Dimension/Character/02_Female/P0000/02_Head/A/0000/Battle/SK_F_P0000_Head_A0000_";

        public override string PrepareReferencePath()
        {
            // Assets/Art_Resources/Domestic/Dimension/Character/01_Male/P0000/05_Onesie/A/0011/Show/SK_M_P0000_Onesie_A0011_Show2.fbx
            // Assets/Art_Resources/Domestic/Dimension/Character/01_Male/P0000/02_Head/A/0000/Show/SK_M_P0000_Head_A0000_Show2.fbx


            // Assets/Art_Resources/Domestic/Dimension/Character/02_Female/P0000/05_Onesie/A/0001/Show/SK_F_P0000_Onesie_A0001_Show0.fbx
            // Assets/Art_Resources/Domestic/Dimension/Character/02_Female/P0000/02_Head/A/0000/Show/SK_F_P0000_Head_A0000_Show0.fbx


            string filename = Path.GetFileNameWithoutExtension(AssetPath);
            int index = filename.LastIndexOf('_');
            if (index == -1)
                return null;
            string postfixName = filename.Substring(index).ToUpper();
            int postfix = 0;
            if (postfixName.StartsWith("_SHOW"))
            {
                postfix = 5;
            }
            else if (postfixName.StartsWith("_LOD"))
            {
                postfix = 4;
            }


            if (postfix > 0)
            {
                string prefix = null;

                if (filename.StartsWith("SK_F_"))
                    prefix = postfix == 5 ? Show_SKF : Battle_SKF;
                else if (filename.StartsWith("SK_M_"))
                    prefix = postfix == 5 ? Show_SKM : Battle_SKM;

                if (!string.IsNullOrEmpty(prefix))
                { 
                    return prefix + filename.Substring(filename.Length - postfix) + Path.GetExtension(AssetPath);
                }
            }
            return null;
        }
        private static bool IsNear(Vector3 a, Vector3 b, float epsilon)
        {
            return (a - b).sqrMagnitude < epsilon * epsilon;
        }

        private static int TestBoundary(HeadBoundaryRule.BoundaryDescription boundaryDesc, Vector3 v)
        {
            int count = boundaryDesc.boundary.Count;
            for(int i = 0; i < count; i++)
            {
                if (IsNear(boundaryDesc.boundary[i].position, v, 0.0002f))
                {
                    return i;
                }
            }
            return -1;
        }
        public override object Evaluate(object input)
        {
            if(input is (GameObject, null))
            {
                Debug.LogError($"{AssetPath}, 参考数据加载失败, 数据路径:{PrepareReferencePath()}");
            }
            else if (input is(GameObject go, HeadBoundaryRule.BoundaryDescription boundaryDesc))
            {
                Debug.Log($"检查头身接缝：{AssetPath}");

                SkinnedMeshRenderer[] smrs = go.GetComponentsInChildren<SkinnedMeshRenderer>();
                foreach (var smr in smrs)
                {
                    Mesh mesh = smr.sharedMesh;
                    if(mesh != null)
                    {
                        List<HeadBoundaryRule.VERTEX> bodyBoundary = HeadBoundaryRule.GenerateBoundaryVertexEx(mesh, smr.gameObject.name.ToUpper().StartsWith("SK_M_"));
                        if (bodyBoundary != null)
                        {
                            foreach (var v in bodyBoundary)
                            {
                                int index = TestBoundary(boundaryDesc, v.position);
                                if (index >= 0)
                                {
                                    Vector3 headPos = boundaryDesc.boundary[index].position;
                                    if ((headPos.x != v.position.x || headPos.y != v.position.y || headPos.z != v.position.z))
                                    {
                                        string message = "头身接缝不完全一致：";
                                        if (headPos.x != v.position.x)
                                            message += $"x: {headPos.x} vs {v.position.x}, ";
                                        if (headPos.y != v.position.y)
                                            message += $"y: {headPos.y} vs {v.position.y}, ";
                                        if (headPos.z != v.position.z)
                                            message += $"z: {headPos.z} vs {v.position.z}, ";
                                        Debug.LogError(message);
                                    }
                                }
                            }
                        }
                    }    
                }
            }
            return null;
            //Debug.Log($"### {input.GetType().Name}");
            //throw new NotImplementedException();
        }
    }
}
