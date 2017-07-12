#include "renderlist.h"
#include "renderitem.h"

namespace iris {

RenderList::RenderList()
{
    used.reserve(1000);
    pool.reserve(1000);

    // todo: check how slow this is
    for(int i = 0;i<1000; i++)
        pool.append(new RenderItem());
}

QVector<RenderItem*>& RenderList::getItems()
{
    return renderList;
}

void RenderList::add(RenderItem *item)
{
    renderList.append(item);
}

RenderItem *RenderList::submitMesh(MeshPtr mesh, MaterialPtr mat, QMatrix4x4 worldMatrix)
{
    RenderItem* item;
    if (pool.size()>0) {
        item = pool.takeFirst();
    } else {
        item = new RenderItem();
    }
    item->reset();
    used.push_back(item);

    item->type = RenderItemType::Mesh;
    item->mesh = mesh;
    item->material = mat;
    item->worldMatrix = worldMatrix;

    renderList.append(item);
}

RenderItem *RenderList::submitMesh(MeshPtr mesh, QOpenGLShaderProgram *shader, QMatrix4x4 worldMatrix, int renderLayer)
{
    RenderItem* item;
    if (pool.size()>0) {
        item = pool.takeFirst();
    } else {
        item = new RenderItem();
    }
    item->reset();
    used.push_back(item);

    item->type = RenderItemType::Mesh;
    item->mesh = mesh;
    item->worldMatrix = worldMatrix;
    item->renderLayer = renderLayer;

    renderList.append(item);
}

void RenderList::clear()
{
    renderList.clear();

    for(auto item : used)
        pool.append(item);
    used.clear();
}

RenderList::~RenderList()
{

}



}
