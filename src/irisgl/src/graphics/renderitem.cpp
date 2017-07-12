#include "renderitem.h"
#include "material.h"

namespace iris {

void RenderItem::reset()
{
    type = RenderItemType::None;
    shaderProgram = nullptr;
    material.clear();
    mesh.clear();
    worldMatrix.setToIdentity();

    shaderProgram = nullptr;
    renderStates = RenderStates();

    cullable = false;
    renderLayer = (int)RenderLayer::Opaque;
}

}
