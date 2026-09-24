using UnityEngine;
using UnityEngine.Rendering;

internal sealed partial class OutdoorSceneBuilder
{
    private void BuildLightingAndCameras()
    {
        var rng = new OutdoorRandom(seed, 1);
        var lighting = Group(root, "05_Lighting");
        var sun = new GameObject("Sun", typeof(Light)).GetComponent<Light>();
        sun.transform.SetParent(lighting, false);
        sun.transform.localRotation = Quaternion.Euler(rng.Range(40f, 60f), rng.Range(5f, 85f), 0f);
        sun.type = LightType.Directional;
        sun.intensity = theme == OutdoorSceneGeneratorTool.Theme.DesertIndustry ? rng.Range(1.15f, 1.45f) : rng.Range(1f, 1.32f);
        sun.color = theme == OutdoorSceneGeneratorTool.Theme.AlienColony ? new Color(rng.Range(0.72f, 0.9f), rng.Range(0.82f, 0.97f), 1f) : new Color(1f, rng.Range(0.88f, 0.98f), rng.Range(0.72f, 0.92f));
        sun.shadows = LightShadows.Soft;
        sun.shadowStrength = 0.65f;
        sun.shadowBias = 0.04f;
        RenderSettings.sun = sun;
        RenderSettings.skybox = null;
        RenderSettings.fog = false;
        RenderSettings.ambientMode = AmbientMode.Trilight;
        RenderSettings.ambientSkyColor = new Color(0.64f, 0.7f, 0.78f);
        RenderSettings.ambientEquatorColor = new Color(0.46f, 0.51f, 0.57f);
        RenderSettings.ambientGroundColor = new Color(0.29f, 0.32f, 0.36f);
        RenderSettings.ambientIntensity = 1f;
        RenderSettings.reflectionIntensity = 0f;

        Bounds bounds = GeometryBounds(root);
        var cameras = Group(root, "06_Cameras");
        for (int i = 0; i < 12; i++)
        {
            bool overview = i < 6;
            string name = overview ? $"Overview_{i + 1:D2}" : $"Detail_{i - 5:D2}";
            var camera = new GameObject(name, typeof(Camera), typeof(AudioListener)).GetComponent<Camera>();
            camera.transform.SetParent(cameras, false);
            camera.orthographic = false;
            camera.fieldOfView = rng.Range(45f, 52f);
            camera.nearClipPlane = 0.3f;
            camera.farClipPlane = 800f;
            camera.clearFlags = CameraClearFlags.SolidColor;
            camera.backgroundColor = theme == OutdoorSceneGeneratorTool.Theme.ThreeLaneValley ? new Color(0.18f, 0.25f, 0.24f) :
                theme == OutdoorSceneGeneratorTool.Theme.DesertIndustry ? new Color(0.32f, 0.28f, 0.22f) : new Color(0.12f, 0.15f, 0.22f);
            camera.allowHDR = false;
            camera.allowMSAA = true;
            camera.enabled = i == 0;
            camera.tag = i == 0 ? "MainCamera" : "Untagged";
            camera.GetComponent<AudioListener>().enabled = i == 0;
            float yaw = 45f + (i % 6) * 60f + rng.Range(-9f, 9f);
            float pitch = rng.Range(52f, 63f);
            camera.transform.rotation = Quaternion.Euler(pitch, yaw, 0f);
            Vector3 target;
            float distance;
            if (overview)
            {
                target = bounds.center + new Vector3(rng.Range(-8f, 8f), 0f, rng.Range(-8f, 8f));
                distance = FitDistance(camera, bounds, target);
            }
            else
            {
                target = cameraTargets.Count > 0 ? cameraTargets[(i - 6) % cameraTargets.Count] : Vector3.zero;
                target += new Vector3(rng.Range(-3f, 3f), 1.5f, rng.Range(-3f, 3f));
                distance = rng.Range(58f, 76f);
            }
            camera.transform.position = target - camera.transform.forward * distance;
        }
    }

    internal static Bounds GeometryBounds(Transform root)
    {
        var renderers = root.GetComponentsInChildren<MeshRenderer>();
        if (renderers.Length == 0) return new Bounds(Vector3.zero, Vector3.one);
        Bounds bounds = renderers[0].bounds;
        for (int i = 1; i < renderers.Length; i++) bounds.Encapsulate(renderers[i].bounds);
        return bounds;
    }

    private static float FitDistance(Camera camera, Bounds bounds, Vector3 target)
    {
        Quaternion inverse = Quaternion.Inverse(camera.transform.rotation);
        float tangent = Mathf.Tan(camera.fieldOfView * Mathf.Deg2Rad * 0.5f) * 0.9f;
        float distance = 1f;
        for (int corner = 0; corner < 8; corner++)
        {
            var p = new Vector3((corner & 1) == 0 ? bounds.min.x : bounds.max.x,
                (corner & 2) == 0 ? bounds.min.y : bounds.max.y,
                (corner & 4) == 0 ? bounds.min.z : bounds.max.z);
            var local = inverse * (p - target);
            distance = Mathf.Max(distance, Mathf.Abs(local.x) / tangent - local.z,
                Mathf.Abs(local.y) / tangent - local.z, camera.nearClipPlane + 1f - local.z);
        }
        return distance;
    }
}
