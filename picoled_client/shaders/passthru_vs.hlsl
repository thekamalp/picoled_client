// Passthrough vertices

struct VSIN {
    float2 pos : POSITION;
};

struct VSOUT {
    float4 pos : SV_POSITION;
    float2 tcoord : TEXCOORD0;
};

VSOUT main(VSIN inp)
{
    VSOUT outp;
    outp.pos = float4(inp.pos.xy, 0.0, 1.0);
    outp.tcoord = inp.pos.xy;
    outp.tcoord.y = -outp.tcoord.y;
    outp.tcoord = 0.5 * (outp.tcoord + 1.0);
    return outp;
}