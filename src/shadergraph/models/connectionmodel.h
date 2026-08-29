#ifndef CONNECTION_MODEL_H
#define CONNECTION_MODEL_H

#include <QString>

class SocketModel;
class ConnectionModel
{
public:
	QString id;

	SocketModel* leftSocket;
	SocketModel* rightSocket;

	ConnectionModel();
};

#endif// CONNECTION_MODEL_H