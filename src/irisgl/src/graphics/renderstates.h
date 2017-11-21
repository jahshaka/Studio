#ifndef RENDERSTATES_H
#define RENDERSTATES_H

#include "blendstate.h"
#include "rasterizerstate.h"
#include "depthstate.h"

namespace iris{

enum class FaceCullingMode
{
    None,
    Front,
    Back,
    FrontAndBack
};

struct RenderStates
{
    int renderLayer;
    BlendState blendState;
    DepthState depthState;
    RasterizerState rasterState;

    bool fogEnabled;
    bool castShadows;
    bool receiveShadows;
    bool receiveLighting;

    RenderStates()
    {
        renderLayer = 0;
        blendState = BlendState::Opaque;
        depthState = DepthState::Default;
        rasterState = RasterizerState::CullCounterClockwise;

        fogEnabled = true;
        castShadows = true;
        receiveShadows = true;
        receiveLighting = true;
    }
};

}

#endif // RENDERSTATES_H
