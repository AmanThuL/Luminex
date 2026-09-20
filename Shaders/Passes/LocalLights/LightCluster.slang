// Safe-math froxel geometry shared by the count and fill kernels. Every float operation is the
// literal transcription of Source/Render/LightClusters.cpp's ordered fp32 sequence: no min/max/dot
// intrinsic, no matrix product and no reciprocal shortcut, so the GPU grid equals the CPU mirror
// bit for bit. The prefix joins the safe-math shader set in xmake/shaders.lua and the backend.
import SceneTables;

static const uint kClusterTilesX = 16u;
static const uint kClusterTilesY = 9u;
static const uint kClusterSliceCount = 24u;
static const uint kClusterTruncatedBit = 0x80000000u;

// One froxel's range in the flat index list; mirrors lmx::render::ClusterRecord.
struct ClusterRecord
{
    uint offset;
    uint count;
};

// Mirrors the fixed ABI block LightClusterStage.cpp uploads. Matrices arrive as their four glm
// columns so `column[c][r]` names exactly the element `m[c][r]` names on the CPU, and the 25
// boundary depths ride in float4s because a cbuffer pads a scalar array element to 16 bytes.
struct LightClusterParams
{
    float4 view[4];
    float4 inverseProjection[4];
    uint rowCount;
    uint activeWidth;
    uint activeHeight;
    uint perClusterCap;
    uint globalCapacity;
    uint froxelCount;
    uint padding0;
    uint padding1;
    float4 sliceDepth[7];
};

// View-space bounds of one froxel; `open` selects the last slice's conservative rule.
struct FroxelBounds
{
    float3 minimum;
    float3 maximum;
    float nearDistance;
    bool open;
    bool empty;
};

float clusterSliceBoundary(LightClusterParams params, uint boundary)
{
    return params.sliceDepth[boundary >> 2u][boundary & 3u];
}

// Literal comparisons rather than min/max intrinsics, exactly as the mirror's lesser/greater.
float lesser(float a, float b)
{
    return a < b ? a : b;
}

float greater(float a, float b)
{
    return a > b ? a : b;
}

// One ordered fp32 4x4 transform of a world point into view space.
float3 transformViewPoint(LightClusterParams params, float3 point)
{
    float3 result = float3(0.0f, 0.0f, 0.0f);
    for (uint r = 0u; r < 3u; ++r)
    {
        float x = params.view[0][r] * point.x;
        float y = params.view[1][r] * point.y;
        float z = params.view[2][r] * point.z;
        float xy = x + y;
        float xyz = xy + z;
        result[r] = xyz + params.view[3][r];
    }
    return result;
}

// Unprojects one NDC corner; the per-component division is exactly rounded, unlike a shared
// reciprocal, which is why safe math reproduces the mirror here.
float3 unprojectCorner(LightClusterParams params, float ndcX, float ndcY, float depth)
{
    float clip[4];
    for (uint r = 0u; r < 4u; ++r)
    {
        float x = params.inverseProjection[0][r] * ndcX;
        float y = params.inverseProjection[1][r] * ndcY;
        float z = params.inverseProjection[2][r] * depth;
        float xy = x + y;
        float xyz = xy + z;
        clip[r] = xyz + params.inverseProjection[3][r];
    }
    return float3(clip[0] / clip[3], clip[1] / clip[3], clip[2] / clip[3]);
}

// Squared distance from `centre` to the closed box, in the mirror's operation order.
float boxDistanceSquared(float3 minimum, float3 maximum, float3 centre)
{
    float delta[3];
    for (uint axis = 0u; axis < 3u; ++axis)
    {
        delta[axis] = 0.0f;
        if (centre[axis] < minimum[axis])
        {
            delta[axis] = minimum[axis] - centre[axis];
        }
        else if (centre[axis] > maximum[axis])
        {
            delta[axis] = centre[axis] - maximum[axis];
        }
    }
    float x = delta[0] * delta[0];
    float y = delta[1] * delta[1];
    float z = delta[2] * delta[2];
    float xy = x + y;
    return xy + z;
}

// `tile`'s half-open pixel-edge span, in unsigned arithmetic; an empty span owns no pixel.
uint2 clusterTileEdges(uint tile, uint tileCount, uint extent)
{
    uint low = (tile * extent + tileCount - 1u) / tileCount;
    uint high = ((tile + 1u) * extent + tileCount - 1u) / tileCount;
    return uint2(low, high);
}

FroxelBounds froxelBounds(LightClusterParams params, uint tileX, uint tileY, uint slice)
{
    uint2 edgesX = clusterTileEdges(tileX, kClusterTilesX, params.activeWidth);
    uint2 edgesY = clusterTileEdges(tileY, kClusterTilesY, params.activeHeight);
    FroxelBounds bounds;
    bounds.minimum = float3(0.0f, 0.0f, 0.0f);
    bounds.maximum = float3(0.0f, 0.0f, 0.0f);
    bounds.nearDistance = 0.0f;
    bounds.open = false;
    bounds.empty = false;
    float nearBoundary = clusterSliceBoundary(params, slice);
    float farBoundary = clusterSliceBoundary(params, slice + 1u);
    // A tile owning no pixel, or a slice the near plane collapsed, can never be looked up.
    if (edgesX.x == edgesX.y || edgesY.x == edgesY.y || nearBoundary == farBoundary)
    {
        bounds.empty = true;
        return bounds;
    }
    float left = 2.0f * float(edgesX.x) / float(params.activeWidth) - 1.0f;
    float right = 2.0f * float(edgesX.y) / float(params.activeWidth) - 1.0f;
    float top = 1.0f - 2.0f * float(edgesY.x) / float(params.activeHeight);
    float bottom = 1.0f - 2.0f * float(edgesY.y) / float(params.activeHeight);
    bool open = slice + 1u == kClusterSliceCount;
    float depths[2] = { nearBoundary, farBoundary };

    bounds.open = open;
    // The mirror's std::numeric_limits<float>::max() sentinels, written as their exact bit pattern.
    float largest = asfloat(0x7F7FFFFFu);
    bounds.minimum = float3(largest, largest, largest);
    bounds.maximum = float3(-largest, -largest, -largest);
    uint faces = open ? 1u : 2u;
    for (uint face = 0u; face < faces; ++face)
    {
        for (uint corner = 0u; corner < 4u; ++corner)
        {
            float ndcX = (corner & 1u) != 0u ? right : left;
            float ndcY = (corner & 2u) != 0u ? bottom : top;
            float3 point = unprojectCorner(params, ndcX, ndcY, depths[face]);
            for (uint axis = 0u; axis < 3u; ++axis)
            {
                bounds.minimum[axis] = lesser(bounds.minimum[axis], point[axis]);
                bounds.maximum[axis] = greater(bounds.maximum[axis], point[axis]);
            }
        }
    }
    // The nearest corner of the open slice's near face; the camera looks down -Z.
    bounds.nearDistance = -bounds.maximum.z;
    return bounds;
}

bool froxelIntersectsSphere(FroxelBounds bounds, float3 centre, float radius)
{
    float radiusSquared = radius * radius;
    if (!bounds.open)
    {
        return boxDistanceSquared(bounds.minimum, bounds.maximum, centre) <= radiusSquared;
    }
    // The open slice widens linearly with view distance, so scaling its near face by the sphere's
    // far extent covers every part of the froxel the sphere can touch. The rejection below is
    // analytically redundant and NOT redundant in fp32: the rounded difference can equal the
    // radius exactly and admit an on-axis sphere, so the kernel keeps the mirror's compare.
    float centreDistance = -centre.z;
    float farDistance = centreDistance + radius;
    if (farDistance < bounds.nearDistance)
    {
        return false;
    }
    float scale = farDistance / bounds.nearDistance;
    float minX = bounds.minimum.x * scale;
    float maxX = bounds.maximum.x * scale;
    float minY = bounds.minimum.y * scale;
    float maxY = bounds.maximum.y * scale;
    float3 minimum = float3(lesser(bounds.minimum.x, minX), lesser(bounds.minimum.y, minY),
                            -farDistance);
    float3 maximum = float3(greater(bounds.maximum.x, maxX), greater(bounds.maximum.y, maxY),
                            bounds.maximum.z);
    return boxDistanceSquared(minimum, maximum, centre) <= radiusSquared;
}

// The froxel a flat index names; the flat order is `(slice * kClusterTilesY + y) * kClusterTilesX + x`.
uint3 froxelCoordinate(uint froxel)
{
    uint x = froxel % kClusterTilesX;
    uint row = froxel / kClusterTilesX;
    uint y = row % kClusterTilesY;
    uint slice = row / kClusterTilesY;
    return uint3(x, y, slice);
}
