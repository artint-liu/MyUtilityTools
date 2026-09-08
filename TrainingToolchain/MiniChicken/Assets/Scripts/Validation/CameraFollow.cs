using UnityEngine;

namespace MiniChicken.Validation
{
    /// <summary>
    /// 追踪相机：第三人称跟随机器人。
    /// 机器人每回合会销毁重建，目标引用失效时按 targetName 自动重新查找。
    /// </summary>
    public class CameraFollow : MonoBehaviour
    {
        [Tooltip("跟随目标；为空时按 targetName 自动查找")]
        public Transform target;
        [Tooltip("目标丢失时用于自动查找的物体名（机器人根节点固定为 BipedRobot）")]
        public string targetName = "BipedRobot";
        [Tooltip("相机与目标的水平距离")]
        public float distance = 4.5f;
        [Tooltip("相机相对目标的高度")]
        public float height = 2.0f;
        [Tooltip("位置平滑时间常数（秒）")]
        public float smoothTime = 0.25f;

        Vector3 velocity;

        void LateUpdate()
        {
            if (target == null)
            {
                var go = GameObject.Find(targetName);
                if (go != null) target = go.transform;
            }
            if (target == null) return;

            // 以目标朝向为基准的追踪视角（只在水平面内旋转，避免俯仰抖动）
            Vector3 fwd = target.forward;
            fwd.y = 0f;
            if (fwd.sqrMagnitude < 0.01f) fwd = Vector3.forward;
            fwd.Normalize();

            Vector3 desired = target.position - fwd * distance + Vector3.up * height;
            transform.position = Vector3.SmoothDamp(transform.position, desired, ref velocity, smoothTime);
            transform.LookAt(target.position);
        }
    }
}
