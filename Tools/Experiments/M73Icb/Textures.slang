struct Args { Texture2D<float4> textures[8]; SamplerState samp; };
ParameterBlock<Args> args : register(b0);
RWStructuredBuffer<float4> outColor : register(u1);
[shader("compute")]
[numthreads(1,1,1)]
void main(uint3 tid : SV_DispatchThreadID) { outColor[tid.x] = args.textures[tid.x].SampleLevel(args.samp, float2(0.5),0); }
