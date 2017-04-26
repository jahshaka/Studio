#ifndef POSTPROCESS_H
#define POSTPROCESS_H

#include "../irisglfwd.h"
#include <QEnableSharedFromThis>

class QOpenGLShader;

namespace iris
{

class Property;

class PostProcessPass
{
    Texture2DPtr output;
    QOpenGLShader* shader;
    QString name;

public:
    void process()
    {

    }
};

class PostProcessContext;

class PostProcess : public QEnableSharedFromThis<PostProcess>
{
public:
    bool enabled;
    QString name;
    QString displayName;

    QString getName()
    {
        return name;
    }

    QString getDisplayName()
    {
        return displayName;
    }

    virtual void process(PostProcessContext* ctx)
    {

    }

    virtual QList<Property*> getProperties()
    {
        return QList<Property*>();
    }

    virtual void setProperty(Property* prop)
    {

    }

};

}

#endif // POSTPROCESS_H
