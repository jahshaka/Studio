#ifndef IVIEWSOURCE_H
#define IVIEWSOURCE_H


namespace iris{

class IViewSource
{
public:
    virtual QVector3D getPosition() = 0;
    virtual QMatrix4x4 getViewMatrix() = 0;
    virtual QMatrix4x4 getProjMatrix() = 0;
    virtual bool supportsVr();
    virtual float getNearClip();
    virtual float getFarClip();
};

}

#endif // IVIEWSOURCE_H
