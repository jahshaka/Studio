#ifndef SHADER_GENERATOR_H
#define SHADER_GENERATOR_H

#include <QString>
#include <QVector>
#include <QMap>

class ShaderContext;
class NodeModel;
class NodeGraph;

struct NodePreviewShaderCache
{
	QString nodeId;
	QString code;
};

struct GeneratedShader
{
	QString vertexShader;
	QString fragmentShader;
};

// for one-time use
class ShaderGenerator
{
	QString vertexShader;
	QString fragmentShader;

public:
	void generateShader(NodeGraph* graph);

	void generateProperties(NodeGraph* graph, ShaderContext* ctx);

	void preprocess();

	void processNode(NodeModel* node, ShaderContext* ctx);

	void postprocess();

	QString processVertexShader(NodeGraph* graph);

	QString processFragmentShader(NodeGraph* graph);

	QString getVertexShader()
	{
		return vertexShader;
	}

	QString getFragmentShader()
	{
		return fragmentShader;
	}

	QMap<QString, QString> shaderPreviews;
	QVector<QString> errorList;

private:
	void processMasterNode(NodeModel* node, ShaderContext* ctx);
};

#endif// SHADER_GENERATOR_H