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

					  // used to store results of calculations
					  // usually only right nodes have these assigned
	QString varName;
	// used to get calculation results
	// will sometimes be var name
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

	virtual QString convertVarTo(SocketModel* toModel)
	{
		return varName;
	}

	virtual QString convertValueTo(SocketModel* toModel)
	{
		return value;
	}

	virtual SocketModel* duplicate() = 0;

	void setGraph(NodeGraph *value);
	NodeGraph *getGraph() const;

	// assigned by shader model context
	QString getValue() const;
	void setValue(const QString &value);

	QString getVarName() const;
	void setVarName(const QString &value);

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