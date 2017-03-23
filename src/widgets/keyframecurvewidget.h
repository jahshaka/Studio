#ifndef KEYFRAMECURVEWIDGET_H
#define KEYFRAMECURVEWIDGET_H

#include <QWidget>
#include <QPen>
#include "../irisgl/src/irisglfwd.h"

namespace Ui {
class KeyFrameCurveWidget;
}

class KeyFrameLabelTreeWidget;
class AnimationWidgetData;

class KeyFrameCurveWidget : public QWidget
{
    Q_OBJECT

    QColor bgColor;
    QColor propColor;
    QColor itemColor;

    QPen linePen;
    QPen cursorPen;

    KeyFrameLabelTreeWidget *labelWidget;
    AnimationWidgetData* animWidgetData;

    bool dragging;
    iris::SceneNodePtr obj;
    QPoint mousePos;
    QPoint clickPos;

    iris::FloatKey* selectedKey;

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

private:
    void drawGrid(QPainter& paint);

private:
    Ui::KeyFrameCurveWidget *ui;
};

#endif // KEYFRAMECURVEWIDGET_H
