shader
{
	pass
	{
		vertex="vs_main";
		pixel="ps_main";
		
		code
		{
			float4x4 matWVProj;
			struct VS_INPUT
			{
				float4 pos : POSITION;
			};
			
			struct VS_OUTPUT
			{
				float4 pos : SV_POSITION;
			};
			
			void vs_main(VS_INPUT i, out VS_OUTPUT o)
			{
				o.pos = mul(matWVProj, i.pos);
			}
			
			float4 ps_main(VS_OUTPUT i) : SV_Target
			{
				return float4(1, 0.5, 0.5, 1);
			}
		}
	}
}