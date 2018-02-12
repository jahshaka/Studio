#ifndef RENDERSTATES_H
#define RENDERSTATES_H

#include "blendstate.h"
#include "rasterizerstate.h"
#include "depthstate.h"

namespace iris{

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

	RenderStates();
};

}

#endif // RENDERSTATES_H
