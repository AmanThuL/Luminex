import RojoRHI;

struct BloomThresholdRoot
{
    rojoRHI::Addr paramsAddr;
    uint sceneColor;
    uint destination;
};

struct BloomDownsampleRoot
{
    rojoRHI::Addr paramsAddr;
    uint source;
    uint destination;
};

struct BloomUpsampleRoot
{
    rojoRHI::Addr paramsAddr;
    uint base;
    uint small;
    uint destination;
    uint padding0;
    uint padding1;
    uint padding2;
};

struct BloomThresholdParams
{
    float threshold;
    uint srcWidth;
    uint srcHeight;
    uint dstWidth;
    uint dstHeight;
};

struct BloomDownsampleParams
{
    uint srcWidth;
    uint srcHeight;
    uint dstWidth;
    uint dstHeight;
};

struct BloomUpsampleParams
{
    uint smallWidth;
    uint smallHeight;
    uint dstWidth;
    uint dstHeight;
};
