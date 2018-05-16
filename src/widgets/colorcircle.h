#ifndef COLORCIRCLE_H
#define COLORCIRCLE_H

#include <QPushButton>
#include <QWidget>

class ColorCircle : public QWidget
{
    Q_OBJECT
public:
    explicit ColorCircle(QWidget *parent = nullptr, QColor ic = nullptr);
    void drawSmallCircle(QColor color);
    void updateColor(int h, int s);
    void setValueInColor(QColor color);
    void setInitialColor(QColor ic);

signals:
    void positionChanged(QColor color);

protected:
    void paintEvent(QPaintEvent *event);
    void mouseMoveEvent(QMouseEvent *event);
    void mousePressEvent(QMouseEvent *event);

private:
    QImage *image;
    int radius, v, alpha=255;
    qreal s;
    QPoint centerPoint;
    QColor color, initialColor, currentColor;
    QPushButton *resetButton, *currentColorButton;

    QPoint pos;
    void drawCircleColorBackground();
    void configureResetButton();
    QColor getCurrentColorFromPosition();

public slots:
    void setOpacity(int a);
};

#endif // COLORCIRCLE_H
