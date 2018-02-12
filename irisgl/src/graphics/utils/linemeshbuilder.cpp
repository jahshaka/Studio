#include "linemeshbuilder.h"
#include "../vertexlayout.h"

namespace iris {



void LineMeshBuilder::addLine(QVector3D a, QVector3D b)
{
    lineData.append(a);
    lineData.append(b);
}

MeshPtr LineMeshBuilder::build()
{
    auto layout = new VertexLayout();
    layout->addAttrib(VertexAttribUsage::Position, GL_FLOAT, 3, sizeof(float)*3);

    auto mesh = Mesh::create(lineData.data(), lineData.size() * sizeof(QVector3D), lineData.size(), layout);
    mesh->setPrimitiveMode(PrimitiveMode::Lines);
    return MeshPtr(mesh);
}

}
