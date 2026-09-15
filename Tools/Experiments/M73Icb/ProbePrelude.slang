ByteAddressBuffer nativeArgs : register(t0);
void encode(ByteAddressBuffer nativeArgs, uint tid) {
    __requirePrelude("#include <metal_command_buffer>");
    __requirePrelude("struct NativeICBArgs { command_buffer commands; }; void encodeNativeICB(device uint* rawArgs, uint tid) { device NativeICBArgs* args = reinterpret_cast<device NativeICBArgs*>(rawArgs); render_command cmd(args->commands, tid); cmd.reset(); cmd.draw_primitives(primitive_type::triangle, 0, 3, 1, 0); }");
    __intrinsic_asm "encodeNativeICB($0, $1)";
}
[shader("compute")]
[numthreads(1,1,1)]
void main(uint3 tid : SV_DispatchThreadID) { encode(nativeArgs, tid.x); }
