#ifndef MESHPROPERTYWIDGET_H
#define MESHPROPERTYWIDGET_H

#include <QWidget>
#include <QSharedPointer>

#include "../../irisgl/src/irisglfwd.h"
#include "../accordianbladewidget.h"

class MeshPropertyWidget : public AccordianBladeWidget
{
    Q_OBJECT

public:
    MeshPropertyWidget();
    ~MeshPropertyWidget();

    void setSceneNode(iris::SceneNodePtr sceneNode);

protected slots:
    void onMeshPathChanged(const QString&);

private:
    QSharedPointer<iris::SceneNode> sceneNode;
    FilePickerWidget* meshPicker;
};

#endif // MESHPROPERTYWIDGET_H
