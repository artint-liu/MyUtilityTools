using System;
using System.IO;
using UnityEditor;
using UnityEngine;

namespace ResourceChecker
{
    class FBXLoaderRule : Rule
    {
        public override string Name => "$FBX加载器";

        public override string Description => "实现FBX加载为GameObject功能，便于后续检查";

        public override string Filter
        {
            get => _filter;
            set => _filter = value;
        }
        private string _filter = "fbx";

        public override Type InputType => typeof(FileInfo);
        public override Type OutputType => typeof(GameObject);

        public static string ToAssetPath(FileInfo fileInfo)
        {
            string absolutePath = fileInfo.FullName.Replace('\\', '/');

            // 获取Assets文件夹的绝对路径
            string assetsAbsolutePath = Application.dataPath;

            // 检查文件是否在Assets目录下
            if (!absolutePath.StartsWith(assetsAbsolutePath))
            {
                Debug.LogError("File is not inside project's Assets folder: " + absolutePath);
                return null;
            }

            // 转换到相对路径并添加"Assets/"前缀
            return "Assets" + absolutePath.Substring(assetsAbsolutePath.Length);
        }
        public override object Resolve(FileInfo fileInfo)
        {
            GameObject result = AssetDatabase.LoadAssetAtPath<GameObject>(ToAssetPath(fileInfo));
            return result;
        }
    }
}
