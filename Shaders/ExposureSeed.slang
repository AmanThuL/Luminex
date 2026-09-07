// Shifts and sets the persistent exposure pair {applied, previous}: `previous` takes what the
// buffer held as `applied`, and `applied` takes exp2(manual EV). One thread, so the two writes are
// ordered by nothing more than program order in a single invocation.
//
// Renderer.cpp declares this pass on two occasions. In auto-exposure mode it is spec 9's reset
// rule -- the frame one of the four reset triggers fires (first frame, scene switch, auto-exposure
// enable, resize) -- and it is what restarts the metering loop. In manual mode it runs on every
// temporal frame (M6.2 spec 7), so the buffer always records the manual exposure the scene pass is
// about to apply *and* the one it applied last: a manual EV edit is a brightness change the
// reprojected history has to be corrected by, and the ratio of the pair is that correction. Either
// way it is an ordinary compute dispatch, not a CPU/GPU stall, which is what keeps the reset path
// inside the three-frames-in-flight contract everything else in the frame already honours.
//
// With temporal off in manual mode the pass is not declared at all, which is what keeps the
// pre-temporal frame's declaration exact; the frame that enables temporal is a history reset, so
// nothing ever reads a `previous` left stale by the frames in between.
//
// Slots: buffer 0 exposure (read-write), 1 params.

struct ExposureSeedParams
{
    float exposure; // exp2(manual EV), computed on the CPU once per declaring frame.
};

[[vk::binding(0, 0)]]
RWStructuredBuffer<float> gExposure : register(u0); // → MSL [[buffer(0)]]

[[vk::binding(1, 0)]]
ConstantBuffer<ExposureSeedParams> gParams : register(b1); // → MSL [[buffer(1)]]

[shader("compute")]
[numthreads(1, 1, 1)]
void computeExposureSeed(uint3 tid: SV_DispatchThreadID)
{
    gExposure[1] = gExposure[0];
    gExposure[0] = gParams.exposure;
}
