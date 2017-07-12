#ifndef RENDERSTATES_H
#define RENDERSTATES_H

namespace iris{

enum class FaceCullingMode
{
    None,
    Front,
    Back,
    FrontAndBack
};

enum class BlendType
{
    None,
    Normal,
    Add
};

struct RenderStates
{
    int renderLayer;
    BlendType blendType;
    bool zWrite;
    bool depthTest;
    FaceCullingMode cullMode;
    bool fogEnabled;
    bool castShadows;
    bool receiveShadows;
    bool receiveLighting;

    RenderStates()
    {
        renderLayer = 0;
        blendType = BlendType::None;
        zWrite = true;
        depthTest = true;
        cullMode = FaceCullingMode::Back;
        fogEnabled = true;
        castShadows = true;
        receiveShadows = true;
        receiveLighting = true;
    }
};

}

#endif // RENDERSTATES_H
