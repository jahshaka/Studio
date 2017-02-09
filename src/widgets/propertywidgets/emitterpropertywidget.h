#ifndef EMITTERPROPERTY_H
#define EMITTERPROPERTY_H

#include <QWidget>
#include <QSharedPointer>

#include "../accordianbladewidget.h"

namespace iris {
    class SceneNode;
}

namespace Ui {
    class EmitterPropertyWidget;
}

class EmitterPropertyWidget : public AccordianBladeWidget
{
    Q_OBJECT

public:
    EmitterPropertyWidget();
    ~EmitterPropertyWidget();

protected slots:
    // pass

private:
    //Ui::EmitterPropertyWidget *ui;
    QSharedPointer<iris::SceneNode> sceneNode;
};

#endif // EMITTER_H
