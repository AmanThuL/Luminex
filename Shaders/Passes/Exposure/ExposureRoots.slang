import RojoRHI;

struct ExposureSeedRoot
{
    rojoRHI::Addr exposureAddr;
    rojoRHI::Addr paramsAddr;
};

struct HistogramRoot
{
    rojoRHI::Addr histogramAddr;
    rojoRHI::Addr exposureAddr;
    rojoRHI::Addr paramsAddr;
    uint sceneColor;
    uint padding;
};

struct ExposureResolveRoot
{
    rojoRHI::Addr histogramAddr;
    rojoRHI::Addr exposureAddr;
    rojoRHI::Addr paramsAddr;
    uint padding0;
    uint padding1;
};

struct HistogramParams
{
    float logLuminanceMin;
    float logLuminanceMax;
    uint width;
    uint height;
};

struct ExposureSeedParams
{
    float exposure;
};

struct ExposureResolveParams
{
    float lowPercentile;
    float highPercentile;
    float targetGrey;
    float evMin;
    float evMax;
    float compensationEv;
    float logLuminanceMin;
    float logLuminanceMax;
    float adaptUp;
    float adaptDown;
    float deltaSeconds;
    float pad;
};
