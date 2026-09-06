// First-class ICB type probe, deliberately excluded from scored shader targets.
// An argument block holds an opaque command_buffer; a local render_command selects one command.
// No invented byte layout, storage array of commands, or injected target-language code is used.
struct CommandArguments
{
    command_buffer commands;
};

[[vk::binding(0, 0)]] ParameterBlock<CommandArguments> gArguments : register(b0);

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 threadId : SV_DispatchThreadID)
{
    if (threadId.x != 0)
        return;
    render_command command = render_command(gArguments.commands, threadId.x);
    command.reset();
    command.draw_primitives(primitive_type::triangle, 0, 3, 1, 0);
}
