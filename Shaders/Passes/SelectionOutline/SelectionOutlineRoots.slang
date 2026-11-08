module SelectionOutlineRoots;
import RojoRHI;

public struct MaskParams
{
    public float4x4 viewProj;
    public uint instanceRow;
    public float selected;
    public uint padding[2];
};

public struct OutlineParams
{
    public float radius;
};

public struct SelectionMaskRoot
{
    public rojoRHI::Addr verticesAddr;
    public rojoRHI::Addr instancesAddr;
    public rojoRHI::Addr materialsAddr;
    public rojoRHI::Addr paramsAddr;
    public uint diffuse;
    public uint sampler;
    public uint padding0;
    public uint padding1;
};

public struct SelectionOutlineRoot
{
    public rojoRHI::Addr paramsAddr;
    public uint display;
    public uint mask;
    public uint selectedDepth;
    public uint sceneDepth;
    public uint padding0;
    public uint padding1;
};

public struct SelectionCopyRoot
{
    public uint display;
    public uint padding0;
    public uint padding1;
    public uint padding2;
};
