#include "linemeshbuilder.h"
#include "../vertexlayout.h"



namespace iris {



void LineMeshBuilder::addLine(QVector3D a, QVector3D b)
{
	this->addLine(a, QColor(255, 255, 255), b, QColor(255, 255, 255));
}

void LineMeshBuilder::addLine(QVector3D a, QColor aCol, QVector3D b, QColor bCol)
{
	lineData.append({ a, QVector4D(aCol.redF(), aCol.greenF(), aCol.blueF(), aCol.alphaF()) });
	lineData.append({ b, QVector4D(bCol.redF(), bCol.greenF(), bCol.blueF(), bCol.alphaF()) });
}

MeshPtr LineMeshBuilder::build()
{
    auto layout = new VertexLayout();
    layout->addAttrib(VertexAttribUsage::Position, GL_FLOAT, 3, sizeof(float) * 3);
	layout->addAttrib(VertexAttribUsage::Color, GL_FLOAT, 4, sizeof(float) * 4);

    auto mesh = Mesh::create(lineData.data(), lineData.size() * sizeof(LineData), lineData.size(), layout);
    mesh->setPrimitiveMode(PrimitiveMode::Lines);
    return MeshPtr(mesh);
}

}
