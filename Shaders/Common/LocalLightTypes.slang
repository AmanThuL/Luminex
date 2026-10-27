module LocalLightTypes;

// Mirrors lmx::render::ClusterRecord: one froxel's range in the flat index list.
public struct ClusterRecord
{
    public uint offset;
    public uint count;
};

// Mirrors lmx::render::LocalLightParams (Source/Render/SceneStage.cpp). `mode` is a runtime
// integer rather than a pipeline permutation, following the shadow filter's precedent, so
// switching paths compiles nothing. The grid dimensions and `sliceDepth` are filled every frame
// whatever the mode, so the block is fully defined even where no lookup reads it.
public struct LocalLightParams
{
    public uint mode;      // kLocalLightMode*
    public uint rowCount;  // addressable light rows, including free slots
    public uint gridX;     // froxel columns
    public uint gridY;     // froxel rows
    public uint gridZ;     // depth slices
    public uint activeOriginX;
    public uint activeOriginY;
    public uint activeWidth;
    public uint activeHeight;
    // Reversed-Z boundary depths, one per slice plus the closing boundary; never increasing.
    public float sliceDepth[25];
};
