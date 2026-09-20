// Ordered fp32 source-view box test; no filtered depth access.
struct OcclusionParams {
    float4 sourceRows[4];
    uint sourceWidth; uint sourceHeight; uint levelCount; uint flags;
    float nearGuard; float depthGuard; uint2 padding;
};
struct OcclusionProjection {
    int4 rectangle;
    uint level;
    float zBox;
    uint outcome;
    uint occluded;
};
OcclusionProjection projectOcclusionBounds(float3 minimum, float3 maximum, OcclusionParams params) {
    OcclusionProjection result = { int4(0), 0u, 0.0f, 0u, 0u };
    if ((params.flags & 1u) == 0u) return result;
    if ((params.flags & 2u) == 0u) { result.outcome = 2u; return result; }
    float minX=3.402823466e+38f,minY=minX,maxX=-minX,maxY=-minX;
    for(uint corner=0u;corner<8u;++corner) {
        float3 point=float3((corner&1u)!=0u?maximum.x:minimum.x,
                            (corner&2u)!=0u?maximum.y:minimum.y,
                            (corner&4u)!=0u?maximum.z:minimum.z);
        float4 clip;
        for(uint r=0u;r<4u;++r) {
            float4 row=params.sourceRows[r];
            float x=row.x*point.x,y=row.y*point.y,z=row.z*point.z;
            float xy=x+y,xyz=xy+z;
            clip[r]=xyz+row.w;
        }
        if(!all(isfinite(clip))||clip.w<=params.nearGuard||clip.z>clip.w) {
            result.outcome=3u;return result;
        }
        float nx=clip.x/clip.w,ny=clip.y/clip.w,nz=clip.z/clip.w;
        float hx=nx*0.5f,hy=ny*-0.5f;
        float ux=hx+0.5f,uy=hy+0.5f;
        float x=ux*float(params.sourceWidth),y=uy*float(params.sourceHeight);
        if(!all(isfinite(float3(x,y,nz)))) { result.outcome=3u;return result; }
        minX=min(minX,x);minY=min(minY,y);maxX=max(maxX,x);maxY=max(maxY,y);
        result.zBox=max(result.zBox,nz);
    }
    float left=floor(minX)-1.0f,top=floor(minY)-1.0f;
    float right=ceil(maxX)+1.0f,bottom=ceil(maxY)+1.0f;
    if(left<0.0f||top<0.0f||right>float(params.sourceWidth)||bottom>float(params.sourceHeight)) {
        result.outcome=4u;return result;
    }
    result.rectangle=int4(int(left),int(top),int(right),int(bottom));
    for(uint level=0u;level<params.levelCount;++level) {
        uint shift=level+1u;
        if(((result.rectangle.z-1)>>shift)-(result.rectangle.x>>shift)<=1 &&
           ((result.rectangle.w-1)>>shift)-(result.rectangle.y>>shift)<=1) {
            result.level=level;result.outcome=1u;return result;
        }
    }
    result.outcome=5u;return result;
}
OcclusionProjection testOcclusionBounds(float3 minimum,float3 maximum,OcclusionParams params,
                                         Texture2D<float> pyramid) {
    OcclusionProjection result=projectOcclusionBounds(minimum,maximum,params);
    if(result.outcome!=1u) return result;
    uint shift=result.level+1u;
    float farthest=1.0f;
    for(int y=result.rectangle.y>>shift;y<=((result.rectangle.w-1)>>shift);++y)
        for(int x=result.rectangle.x>>shift;x<=((result.rectangle.z-1)>>shift);++x)
            farthest=min(farthest,pyramid.Load(int3(x,y,int(result.level))));
    float guarded=result.zBox+params.depthGuard;
    result.occluded=guarded<farthest?1u:0u;
    return result;
}
