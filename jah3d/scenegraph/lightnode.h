#ifndef LIGHTNODE_H
#define LIGHTNODE_H

#include <QSharedPointer>

typedef QSharedPointer<LightNode> LightNodePtr;

class LightNode
{
public:
    LightNodePtr create();
private:
    LightNode();
};

#endif // LIGHTNODE_H
