// Pass a single texture out

struct PSIN {
    float4 pos : SV_POSITION;
    float2 tcoord : TEXCOORD0;
};

Texture2D<float4> tex : register(t0);

SamplerState sampleLinear : register(s0) {
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = Wrap;
    AddressV = Wrap;
};

float4 main(PSIN inp) : SV_Target
{
    return tex.Sample(sampleLinear, inp.tcoord);
}