#ifndef SWITCH_H
#define SWITCH_H

#include <QCheckBox>
#include <QMouseEvent>
#include <QWidget>

class Switch : public QCheckBox
{
    Q_OBJECT
    Q_PROPERTY(QColor onColor READ onColor WRITE setOnColor )
    Q_PROPERTY(bool on READ on WRITE setOn NOTIFY onChanged)
    Q_PROPERTY(int startPoint READ startPoint WRITE setStartPoint)
public:
    Switch(QWidget *parent = 0);
    ~Switch();

    void setOnColor(QColor col);
    QColor onColor();
    void setOn(bool val);
    bool on();
    void setStartPoint(int point);
    int startPoint();

private:
    bool isOn;
    QColor color;
    QColor endColor;
    QRect boundingRect;
    int _startPoint;
    int _endPoint;
    int _width;
    int _height;

public slots:
    void setSize(int h);
    void setColor(QColor color);
    void simulateClick();

signals:
    void onChanged(bool val);

protected:
    void paintEvent(QPaintEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);

};

#endif // SWITCH_H
