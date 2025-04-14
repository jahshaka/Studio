/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef KEYFRAMECURVEWIDGET_H
#define KEYFRAMECURVEWIDGET_H

#include <QWidget>
#include <QPen>
#include "../irisgl/src/irisglfwd.h"

namespace Ui {
class KeyFrameCurveWidget;
}

class QTreeWidgetItems;

class KeyFrameLabelTreeWidget;
class AnimationWidgetData;

enum class DragHandleType
{
    None,
    Point,
    LeftTangent,
    RightTangent
};

class KeyFrameCurveWidget : public QWidget
{
    Q_OBJECT

    QColor bgColor;
    QColor propColor;
    QColor itemColor;

    QPen linePen;
    QPen cursorPen;

    QPen curvePen;

    KeyFrameLabelTreeWidget *labelWidget;
    AnimationWidgetData* animWidgetData;

    bool dragging;
    iris::SceneNodePtr obj;
    QPoint mousePos;
    QPoint clickPos;
    float keyPointRadius;
    float handlePointRadius;
    float handleLength;
    DragHandleType dragHandleType;

    iris::FloatKey* selectedKey;

    QList<iris::FloatKeyFrame*> keyFrames;

    bool leftButtonDown;
    bool middleButtonDown;
    bool rightButtonDown;

public:
    explicit KeyFrameCurveWidget(QWidget *parent = 0);
    ~KeyFrameCurveWidget();

    void setLabelWidget(KeyFrameLabelTreeWidget *value);
    void paintEvent(QPaintEvent *painter);

    void setAnimWidgetData(AnimationWidgetData *value);

    void mousePressEvent(QMouseEvent* evt);
    void mouseReleaseEvent(QMouseEvent* evt);
    void mouseMoveEvent(QMouseEvent* evt);
    void wheelEvent(QWheelEvent* evt);

    int timeToPos(float timeInSeconds);
    float posToTime(int xpos);

    void showEvent(QShowEvent* event);

signals:
	void keyChanged(iris::FloatKey* key);

private:
    void drawGrid(QPainter& paint);
    void drawKeyFrames(QPainter& paint);
    void drawKeys(QPainter& paint);
    void drawKeyHandles(QPainter& paint);

    iris::FloatKey* getKeyAt(int x, int y);
    QPoint getLeftTangentHandlePoint(iris::FloatKey* key);
    QPoint getRightTangentHandlePoint(iris::FloatKey* key);
    QPoint getKeyFramePoint(iris::FloatKey* key);

    // checks if point at x and y hits handle
    bool isLeftHandleHit(iris::FloatKey* key, int x, int y);
    bool isRightHandleHit(iris::FloatKey* key, int x, int y);

private slots:
    void selectedCurveChanged();

private:
    Ui::KeyFrameCurveWidget *ui;
};

#endif // KEYFRAMECURVEWIDGET_H
