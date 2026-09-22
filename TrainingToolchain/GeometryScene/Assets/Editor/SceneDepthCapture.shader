// 深度捕获着色器：把场景渲染为深度灰度图（供编辑器工具 SceneCameraScreenshotTool 使用）。
// 亮度约定：最近处 = 1.0，最远处 / 无限远 = 0.0（背景由相机纯黑清屏实现，即无限远）。
// _DepthMode = 0：ZBuffer 原始深度（按平台处理 reversed-Z，保证各平台近处均为亮度 1）
// _DepthMode = 1：按相机 near/far 归一化的线性深度
Shader "Hidden/SceneDepthCapture"
{
    Properties
    {
        _DepthMode("Depth Mode", Float) = 0
    }
    SubShader
    {
        Tags { "RenderType" = "Opaque" "RenderPipeline" = "UniversalPipeline" "Queue" = "Geometry" }
        Pass
        {
            Name "DepthCapture"
            Cull Back
            ZWrite On
            ZTest LEqual

            HLSLPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"

            CBUFFER_START(UnityPerMaterial)
                float _DepthMode;
            CBUFFER_END

            struct Attributes
            {
                float4 positionOS : POSITION;
            };

            struct Varyings
            {
                float4 positionHCS : SV_POSITION;
                float viewZ : TEXCOORD0; // 正的视空间深度
            };

            Varyings vert(Attributes IN)
            {
                Varyings o;
                VertexPositionInputs vp = GetVertexPositionInputs(IN.positionOS.xyz);
                o.positionHCS = vp.positionCS;
                o.viewZ = -vp.positionVS.z;
                return o;
            }

            float4 frag(Varyings i) : SV_Target
            {
                float b;
                if (_DepthMode < 0.5)
                {
                    // ZBuffer 原始深度（片元中的 SV_POSITION.z 即写入深度缓冲前的原始值）
                    float raw = i.positionHCS.z;
                #if UNITY_REVERSED_Z
                    b = raw;           // reversed-Z 平台：近=1 远=0，亮度直接取原值
                #else
                    b = 1.0 - raw;     // 常规平台：近=0 远=1，需反转
                #endif
                }
                else
                {
                    // 线性深度：按 near/far 归一化到 [0,1] 后反转（近=1 远=0）
                    float nearZ = _ProjectionParams.y;
                    float farZ = _ProjectionParams.z;
                    float linear01 = saturate((i.viewZ - nearZ) / max(farZ - nearZ, 1e-6));
                    b = 1.0 - linear01;
                }
                return float4(b, b, b, 1);
            }
            ENDHLSL
        }
    }
    Fallback Off
}
