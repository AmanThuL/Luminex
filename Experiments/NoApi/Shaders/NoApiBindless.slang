// The prototype's shader-side view of the one bindless table (spec section 5: "texture and sampler
// handles through one bindless table"). Owns no resources and no entry points; every Proto*.slang
// frontend imports it and reads the table through the address its root block carries.
//
// Why one wrapper struct per shader-side type rather than a bare handle array: the Metal compiler
// rejects a pointer whose pointee is a texture ("type 'texture2d<float>' cannot be used in buffer
// pointee type"), which is exactly what Slang emits for
// `ConstBufferPointer<DescriptorHandle<Texture2D>>`. It accepts the same pointer once the handle is
// a *member* of a struct, so `ConstBufferPointer<TextureSlot>` below is the indexable form that
// compiles. See docs/research/2026-08-12-execution-model-evidence.md section 5.9.
//
// Every one of these views addresses the *same* table, at the same eight-byte stride
// (`Capabilities::bindlessSlotStride`, and `sizeof(MTL::ResourceID)`): a slot index means the same
// thing whichever view reads it, and which view a shader uses is purely a statement about what the
// slot's writer put there.
module NoApiBindless;

// A sampled two-dimensional texture slot. Also the view a depth map is read through: Slang lowers
// `Texture2D` plus `SampleCmpLevelZero` to the same `depth2d` reinterpretation the production
// `Shaders/ScenePass.slang` gets for its own `Texture2D gShadowMap`.
public struct TextureSlot
{
    public DescriptorHandle<Texture2D> handle;
};

// A sampled cube map slot (the IBL irradiance and prefiltered-environment sets).
public struct CubeSlot
{
    public DescriptorHandle<TextureCube> handle;
};

// A read-write storage image slot (the bloom chain's mips).
public struct StorageSlot
{
    public DescriptorHandle<RWTexture2D<float4>> handle;
};

// An ordinary filtering sampler slot.
public struct SamplerSlot
{
    public DescriptorHandle<SamplerState> handle;
};

// A depth-comparison sampler slot.
public struct ComparisonSamplerSlot
{
    public DescriptorHandle<SamplerComparisonState> handle;
};

// The vertex layout every draw reads through its root block's `vertices` pointer. Byte-identical to
// `Shaders/ScenePass.slang`'s and `Shaders/ShadowPass.slang`'s own `Vertex`, and to
// `Workload/AssetGen.h`'s `QuadVertex` -- the shared quad is uploaded once and read from an address,
// never bound.
public struct Vertex
{
    public float3 position;
    public float3 normal;
    public float4 tangent; // xyz tangent, w handedness (+1/-1)
    public float2 uv;
};
