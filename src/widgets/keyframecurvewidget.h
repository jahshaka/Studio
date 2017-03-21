#ifndef KEYFRAMECURVEWIDGET_H
#define KEYFRAMECURVEWIDGET_H

#include <QWidget>

namespace Ui {
class KeyFrameCurveWidget;
}

class KeyFrameLabelTreeWidget;
class AnimationWidgetData;

class KeyFrameCurveWidget : public QWidget
{
    Q_OBJECT

    KeyFrameLabelTreeWidget *labelWidget;
    AnimationWidgetData* animWidgetData;

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

private:
    void drawGrid(QPainter& paint);

private:
    Ui::KeyFrameCurveWidget *ui;
};

#endif // KEYFRAMECURVEWIDGET_H
