#ifndef SHADER_CONTEXT_H
#define SHADER_CONTEXT_H

#include <QString>
#include <QList>
#include <QMap>

#include "../graph/nodegraph.h"

class SocketModel;

// temporary variable
struct TempVar
{
	QString name;
	QString typeName;
};

struct CodeChunk
{
	//NodeModel* node = nullptr;
	QString owner;
	QString code;
};

struct ShaderFunction
{
	QString name;
	QString functionBody;
};


struct GeneratedCode
{
	// contains list of registered functions
	QMap<QString, ShaderFunction> functions;
	QString functionsCode;

	QString vertexCode;
	QString fragmentCode;
};

class ShaderContext : public ModelContext
{
	QStringList uniforms;
	// mapped using the socket guid
	QMap<QString, TempVar> tempVars;
	QList<CodeChunk> codeChunks;
	QMap<QString, ShaderFunction> functions;

	//QString code;
	bool debugCode;
public:
	// adds function if it doesnt already exist
	void addFunction(QString name, QString function);

	// created a temporary variable using the socket
	TempVar createTempVar(SocketModel* sock);
	TempVar createTempVar(QString typeName);

	QList<TempVar> getTempVars();
	void clear();

	void addCodeChunk(NodeModel* node, QString code);

	// add declaration for uniform here
	void addUniform(QString uniformDecl);

	QString generateVars();
	QString generateUniforms();
	QString generateFunctionDefinitions();
	QString generateCode(bool debug = false);
	QString generateFragmentCode(bool debug = false);
};

#endif// SHADER_CONTEXT_H