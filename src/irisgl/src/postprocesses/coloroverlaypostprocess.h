#ifndef COLOROVERLAYPOSTPROCESS_H
#define COLOROVERLAYPOSTPROCESS_H

#include "../graphics/postprocess.h"
#include <QVector3D>

class QOpenGLShaderProgram;

namespace  iris {

class ColorOverlayPostProcess;
typedef QSharedPointer<ColorOverlayPostProcess> ColorOverlayPostProcessPtr;

class ColorOverlayPostProcess : public PostProcess
{
    QColor overlayColor;
    QVector3D col;
    QOpenGLShaderProgram* shader;
public:
    ColorOverlayPostProcess();

    virtual void process(PostProcessContext* ctx) override;

    QColor getOverlayColor() const;
    void setOverlayColor(const QColor &value);

    static ColorOverlayPostProcessPtr create();
};

}

#endif // COLOROVERLAYPOSTPROCESS_H
