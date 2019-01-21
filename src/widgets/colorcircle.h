#ifndef COLORCIRCLE_H
#define COLORCIRCLE_H

#include <QPushButton>
#include <QWidget>
#include <QLayout>
#include <QLabel>

class ColorCircle : public QWidget
{
    Q_OBJECT
public:
	QColor color, initialColor, currentColor;

    explicit ColorCircle(QWidget *parent = nullptr, QColor ic = nullptr);
    void drawSmallCircle(QColor color);
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
    int radius, colorValue, alpha=255, offset = 4;
    qreal saturation;
    QPoint centerPoint;    
    QPushButton *resetButton, *currentColorButton;
    QPoint pos;

	void setCirclePosition(QMouseEvent *event);
    void drawCircleColorBackground();
    void configureResetButton();
    QColor getCurrentColorFromPosition();
    QVBoxLayout *layout;
    QLabel *label;

public slots:
    void setOpacity(int a);
};

#endif // COLORCIRCLE_H
