__target_intrinsic(metal, "command_buffer")
struct IcbHandle { uint64_t opaque; }
struct Args { IcbHandle commands; };
ParameterBlock<Args> args : register(b0);
void encode(IcbHandle commands, uint tid) {
    __requirePrelude("#include <metal_command_buffer>");
    __intrinsic_asm "{ render_command cmd($0, $1); cmd.reset(); cmd.draw_primitives(primitive_type::triangle, 0, 3, 1, 0); }";
}
[shader("compute")]
[numthreads(1,1,1)]
void main(uint3 tid : SV_DispatchThreadID) { encode(args.commands, tid.x); }
