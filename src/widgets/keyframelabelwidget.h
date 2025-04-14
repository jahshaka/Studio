/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef KEYFRAMELABELWIDGET_H
#define KEYFRAMELABELWIDGET_H

#include <QWidget>
#include <QDebug>
#include <QPainter>
#include <QMouseEvent>
#include <vector>
#include "../irisgl/src/irisglfwd.h"

namespace Ui {
    class KeyFrameLabelWidget;
}
class KeyFrameLabel;

class KeyFrameLabelWidget:public QWidget
{
    Q_OBJECT

    QColor bgColor;
    QColor itemColor;

    QPen cursorPen;
    iris::SceneNodePtr obj;
public:
    KeyFrameLabelWidget(QWidget* parent);

    void setSceneNode(iris::SceneNodePtr node);
    void setKeyFrameSet(iris::KeyFrameSetPtr frameSet);
    void clearKeyFrameSet();
    void resetKeyFrames();

    //void resizeEvent(QResizeEvent* evt);
    bool eventFilter(QObject *obj, QEvent *evt);

    QList<KeyFrameLabel *> getLabels() const;

private slots:
    void scrollValueChanged(int val);

private:
    /*
     * Deletes all items in layout
     */
    void clearLayout(QLayout* layout);

private:
    Ui::KeyFrameLabelWidget* ui;
    QList<KeyFrameLabel*> labels;
};

#endif // KEYFRAMELABELWIDGET_H
