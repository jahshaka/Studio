#ifndef MATERIALWRITER_H
#define MATERIALWRITER_H

#include <QJsonObject>

class NodeGraph;
class FloatProperty;

class MaterialWriter
{
public:
    QJsonObject serializeMaterial(NodeGraph* graph);

private:
	void writeFloat(FloatProperty* prop, QJsonObject& uniform);
};

#endif
