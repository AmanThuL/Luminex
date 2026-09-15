// Indexed native ICB encoding consumes argument words written by the preceding GPU pass.
ByteAddressBuffer nativeArgs : register(t0);
RWStructuredBuffer<uint> words : register(u1);
struct Frame { uint number; };
ConstantBuffer<Frame> frame : register(b2);
void encode(ByteAddressBuffer native, uint tid, uint count, uint instances, uint first, uint base) {
    __requirePrelude("#include <metal_command_buffer>");
    __requirePrelude("struct NativeICBArgs { command_buffer commands [[id(0)]]; device uint* indices [[id(1)]]; }; void encodeIndexed(device uint* raw, uint tid, uint count, uint instances, uint first, uint base) { device NativeICBArgs* args = reinterpret_cast<device NativeICBArgs*>(raw); render_command cmd(args->commands, tid); cmd.reset(); if (instances) cmd.draw_indexed_primitives(primitive_type::triangle, count, args->indices + first, instances, 0, base); }");
    __intrinsic_asm "encodeIndexed($0, $1, $2, $3, $4, $5)";
}
[shader("compute")]
[numthreads(1,1,1)]
void classify(uint3 tid : SV_DispatchThreadID) {
    uint offset = tid.x * 5;
    words[offset] = 3;
    words[offset+1] = (frame.number % 4 == 3 && tid.x == 1) ? 0 : 1;
    words[offset+2] = tid.x * 3;
    words[offset+3] = 0;
    words[offset+4] = tid.x;
}
[shader("compute")]
[numthreads(1,1,1)]
void emitCommands(uint3 tid : SV_DispatchThreadID) {
    uint offset = tid.x * 5;
    encode(nativeArgs, tid.x, words[offset], words[offset+1], words[offset+2], words[offset+4]);
}
