/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef KEYFRAMEWIDGET_H
#define KEYFRAMEWIDGET_H

#include <QWidget>
#include <QDebug>
#include <QPainter>
#include <QMouseEvent>
#include <vector>
#include <QVector2D>
#include <QSharedPointer>
#include "../irisgl/src/irisglfwd.h"
#include "keyframelabeltreewidget.h"

namespace iris
{
    class SceneNode;
}

class KeyFrameLabelWidget;
class KeyFrameLabelTreeWidget;

class QTreeWidget;
class QTreeWidgetItem;
class AnimationWidgetData;

enum class DopeKeyType
{
    Null,
    FloatKey,
    SummaryKey
};

struct DopeKey
{
    DopeKeyType keyType;
    iris::FloatKey* floatKey = nullptr;
    SummaryKey summaryKey;

    QString propertyName;
    QString subPropertyName;

    DopeKey()
    {
        keyType = DopeKeyType::Null;
    }

    explicit DopeKey(iris::FloatKey* floatKey, QString propName, QString subPropName)
    {
        Q_ASSERT(floatKey!=nullptr);

        this->floatKey = floatKey;
        keyType = DopeKeyType::FloatKey;
        this->propertyName = propName;
        this->subPropertyName = subPropName;

    }

    explicit DopeKey(SummaryKey summaryKey, QString propName, QString subPropName)
    {
        this->summaryKey = summaryKey;
        keyType = DopeKeyType::SummaryKey;
        this->propertyName = propName;
        this->subPropertyName = subPropName;
    }

    void move(float timeIncr);

    bool isNull()
    {
        return keyType == DopeKeyType::Null;
    }

    static DopeKey Null()
    {
        return DopeKey();
    }
};

class KeyFrameWidget:public QWidget
{
    Q_OBJECT

    /**
     * viewLeft and viewRight are like the "camera" of the keyframes
     * they are used for translating keys from their local "key space"
     * to viewport space and vice versa.
     */
    float viewLeft;
    float viewRight;

private:
    QColor bgColor;
    QColor propColor;
    QColor itemColor;

    QPen linePen;
    QPen cursorPen;
    QPen pointPen;

    float maxTimeInSeconds;

    bool dragging;
    int scaleRatio;

    iris::SceneNodePtr obj;
    QPoint mousePos;
    QPoint clickPos;

    //iris::FloatKey* selectedKey;
    DopeKey selectedKey;
    DopeKey contextKey;

    bool leftButtonDown;
    bool middleButtonDown;
    bool rightButtonDown;

    KeyFrameLabelTreeWidget *labelWidget;
    AnimationWidgetData* animWidgetData;

    // pens and brushes
    QBrush defaultBrush;
    QBrush innerBrush;
    QBrush highlightBrush;

    int keyPointSize;

public:
    KeyFrameWidget(QWidget* parent);

    void setSceneNode(iris::SceneNodePtr node);
    void adjustLength();

    float getStartTimeRange();
    float getEndTimeRange();

    float getTimeAtCursor();

    void drawFrame(QPainter& paint, QTreeWidget* tree, QTreeWidgetItem* item, int& yTop);
    void drawBackgroundLines(QPainter& paint);
    int getXPosFromSeconds(float seconds);

    void mousePressEvent(QMouseEvent* evt);
    void mouseReleaseEvent(QMouseEvent* evt);
    void mouseMoveEvent(QMouseEvent* evt);
    void wheelEvent(QWheelEvent* evt);
    //void resizeEvent(QResizeEvent* event);
    void paintEvent(QPaintEvent *painter);
    void drawPoint(QPainter& paint, QPoint point, bool isHighlight = false);

    void setLabelWidget(KeyFrameLabelTreeWidget *value);

    void setAnimWidgetData(AnimationWidgetData *value);

signals:
    void timeRangeChanged(float timeStart, float timeEnd);

protected slots:
    void deleteContextKey();

private:
    /**
     * Finds squared distance between 2 points
     */
    static float distanceSquared(float x1,float y1,float x2,float y2);

    float posToTime(int xpos);
    int timeToPos(float timeInSeconds);

    DopeKey getSelectedKey(int x,int y);
    DopeKey getSelectedKey(QTreeWidget* tree,QTreeWidgetItem* item, int& yTop);
};

#endif // KEYFRAMEWIDGET_H
