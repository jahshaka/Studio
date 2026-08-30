#ifndef SOCKET_HELPER_H
#define SOCKET_HELPER_H

#include <QString>

class SocketModel;
class SocketHelper
{
public:
	static QString convertVectorValue(QString fromValue, SocketModel* from, SocketModel* to);
	static QString getVectorComponent(int index);
	static int getNumComponents(QString valueType);
};


#endif// SOCKET_HELPER_H