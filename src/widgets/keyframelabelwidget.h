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

class KeyFrameLabelWidget:public QWidget
{
    QColor bgColor;
    QColor itemColor;

    QPen cursorPen;
    iris::SceneNodePtr obj;
public:
    KeyFrameLabelWidget(QWidget* parent);

    void setSceneNode(iris::SceneNodePtr node);

    void paintEvent(QPaintEvent *painter);

    void drawFrameLabel(QString name,QPainter* paint,int yBottom);
};

#endif // KEYFRAMELABELWIDGET_H
