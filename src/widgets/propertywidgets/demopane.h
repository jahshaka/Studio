#ifndef DEMOPANE_H
#define DEMOPANE_H

#include <QWidget>
#include <QSharedPointer>
#include "../../irisgl/src/irisglfwd.h"

#include "../accordianbladewidget.h"

/*
 *  DO NOT REMOVE THIS FILE!
 *
 *  This is a testbed of sorts for every pane and custom widget to make sure they
 *  conform and are consistent. If you add a new widget type, add it here as well.
 *
 */

class DemoPane : public AccordianBladeWidget
{
    Q_OBJECT

public:
    DemoPane();

    void setSceneNode(iris::SceneNodePtr sceneNode) {

    }

protected slots:
    // ---

private:
    HFloatSlider* demoSlider;
    ColorValueWidget* demoColor;
    CheckBoxProperty* demoCheck;
    ComboBox* demoCombo;
    TexturePicker* demoPicker;
    TextInput* demoInput;
};

#endif // DEMOPANE_H
