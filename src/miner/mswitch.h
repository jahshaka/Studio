#ifndef MSWITCH_H
#define MSWITCH_H

#include <QPainter>
#include <QWidget>
#include <QSlider>
#include <QPropertyAnimation>

class MSwitch : public QSlider
{
    Q_OBJECT
    Q_PROPERTY(QColor onColor1 READ onColor1 WRITE setOnColor1 )

public:
    MSwitch(QWidget *parent = Q_NULLPTR);
    ~MSwitch();
    void setSizeOfSwitch(int a);
    void setColor(QColor col);
    void setOnColor1(QColor col);
    QColor onColor1();

    void toggle();
    void setChecked(bool val);
    bool isChecked();
    void simulateClick();
private:
    bool isOn = false;
    QRect boundingRect;
    QColor startColor;
    QColor endColor;
    QColor onColor;
    int wHeight, wWidth, factor=10, offset =3;

protected:
    void paintEvent(QPaintEvent *event);

    void mousePressEvent(QMouseEvent *event);

    void mouseReleaseEvent(QMouseEvent *e);

#if QT_CONFIG(wheelevent)
    void wheelEvent(QWheelEvent *e);
#endif

signals:
    void switchPressed(bool b);
    void stateChanged(int val);
};

#endif // MSWITCH_H
