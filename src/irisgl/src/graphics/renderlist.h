#ifndef RENDERLIST_H
#define RENDERLIST_H

#include <QVector>
#include "material.h"

namespace iris {

class RenderItem;

class RenderList
{
    QVector<RenderItem*> pool;
    QVector<RenderItem*> used;

    QVector<RenderItem*> renderList;
public:
    RenderList();
    QVector<RenderItem*>& getItems();

    void add(RenderItem* item);

    RenderItem* submitMesh(MeshPtr mesh, MaterialPtr mat, QMatrix4x4 worldMatrix);

    RenderItem* submitMesh(MeshPtr mesh, QOpenGLShaderProgram* shader, QMatrix4x4 worldTransform, int renderLayer = (int)RenderLayer::Opaque);

    void clear();

    void sort();

    ~RenderList();
};

}

#endif // RENDERLIST_H
