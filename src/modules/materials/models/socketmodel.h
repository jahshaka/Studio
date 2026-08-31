#ifndef SOCKET_MODEL_H
#define SOCKET_MODEL_H

#include <QString>
#include <QColor>
class NodeGraph;
class NodeModel;
class ConnectionModel;
class SocketModel
{
public:
	QString id;
	QString name;
	QString typeName; //todo: change to enum

	// the socket's default value as a GLSL-style literal string (e.g.
	// "vec2(0.0f, 0.0f)") — parsed by BakeProgram for unconnected inputs
	QString value;

	// color for the socket depending on the type of socket - no enum created
	QColor socketColor;

	// connection if any
	ConnectionModel* connection = nullptr;
	NodeGraph* graph = nullptr;

	NodeModel* node = nullptr;

	SocketModel();
	SocketModel(QString name, QString typeName);

	virtual bool canConvertTo(SocketModel* other)
	{
		// todo: make false by default
		return true;
	}

	virtual SocketModel* duplicate() = 0;

	void setGraph(NodeGraph *value);
	NodeGraph *getGraph() const;

	QString getValue() const;
	void setValue(const QString &value);

	bool hasConnection()
	{
		return connection != nullptr;
	}
	ConnectionModel* getConnection()
	{
		return connection;
	}

	SocketModel* getConnectedSocket();
	NodeModel *getNode() const;
	void setNode(NodeModel *value);
};

#endif// SOCKET_MODEL_H