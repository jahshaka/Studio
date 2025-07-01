/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include <QApplication>
#include <QGuiApplication>
#include <QDialog>
#include <QComboBox>
#include <QGraphicsEffect>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QButtonGroup>
#include <QScreen>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QWidget>
#include <QLinearGradient>
#include <QProxyStyle>
#if defined(_WIN32)
#include <qwindowdefs_win.h>
#endif

#include "../misc/stylesheet.h"


class SliderMoveToMouseClickPositionStyle : public QProxyStyle
{
public:
	using QProxyStyle::QProxyStyle;

	int styleHint(QStyle::StyleHint hint,
		const QStyleOption* option = 0,
		const QWidget* widget = 0,
		QStyleHintReturn* returnData = 0) const
	{
        if (hint == QStyle::SH_Slider_AbsoluteSetButtons) 	return (Qt::LeftButton | Qt::MiddleButton | Qt::RightButton);
		return QProxyStyle::styleHint(hint, option, widget, returnData);
	}
};


class ValueSlider : public QSlider
{
public:
    ValueSlider(QWidget *parent = Q_NULLPTR);
    void setColor(QColor col);
private:
    QColor col;
};

class Overlay : public QWidget
{
    Q_OBJECT
public:
    //accepts screen rect and widget as parent
    Overlay(QRect sg,int screenNumber, QWidget* parent = Q_NULLPTR);
private:
	QPixmap pixmap;
	int screenNum;
signals:
    void color(QColor col);
    void closeWindow();
protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
};

class ColorCircle;
class ColorDisplay : public QWidget
{
    Q_OBJECT
public:
    ColorDisplay(QWidget *parent = Q_NULLPTR);
    QColor color;
    QColor initialColor = QColor();
protected:
    void paintEvent(QPaintEvent *) override;
signals:
    void pick();
};

class InputCircle : public QWidget
{
    Q_OBJECT
public:
    InputCircle(ColorCircle *parent = Q_NULLPTR);
    int offset;
    QColor color;
    
    void setInitialColor(QColor col);
    void setColor(QColor color);
    QColor getInitialColor();
    QPoint getGlobalPositionFromIndicatorCircle();

private:
    bool drawSmallDot = false;
    bool pressed = false;
    int squareSize;
    int radius;
//    int padding;
    int alpha = 255;
    
    ColorCircle *parent;
    
    QPoint centerPoint;
    QPoint pos;
    QPoint globalPos;
    
    QColor initialColor;
    int colorValue = 255;
    
    void setCirclePosition(QMouseEvent *);
    QColor getCurrentColorFromPosition();
    void drawIndicatorCircle(QColor color);
    bool checkColorValue(bool val = true);
    
public slots:
    void setRed(int red);
    void setGreen(int green);
    void setBlue(int blue);
    void setAlpha(int alpha);
    void setHue(int hue);
    void setSaturation(int saturation);
    void setValue(int value);

signals:
    void positionChanged(QColor color); //emitted when color circle pressed
    void colorChanged(QColor color); //emitted when sliders change value
    void updateColorText(QColor color); // to update name text
    void selectingColor(bool val);
    void updateValue(int value);
protected:
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void paintEvent(QPaintEvent *) override;
};

class ColorCircle : public QWidget
{
    Q_OBJECT
public:
    explicit ColorCircle(QWidget *parent = nullptr);

    QColor color;
    
    QPoint centerPoint;
    
    int colorValue = 255;
    int radius;
    int offset;
    int padding;
    int squareSize;
    QImage* drawImage();

public slots:
    void setAlpha(int alpha);
    void setColor(QColor col);
private:
    QImage *image = Q_NULLPTR;
    qreal saturation;
    qreal alpha = 255.0;


protected:
    void paintEvent(QPaintEvent *)override;
};



class ColorView : public QWidget
{
    Q_OBJECT
public:
    ColorView(QColor color= QColor(), QWidget *parent = nullptr);
	void showAtPosition(QMouseEvent *event, QColor color);
	static ColorView* getSingleston();

	static ColorView* instance;
private:
    QColor initialColor;
    const qreal factor = 2.55;
    bool picking = false;
    QSlider *rSlider;
    QSlider *gSlider;
    QSlider *bSlider;
    QSlider *aSlider;
    QSlider *hSlider;
    QSlider *sSlider;
    QSlider *vSlider;
    QStackedWidget *stackWidget;

    QSpinBox *rBox;
    QSpinBox *gBox;
    QSpinBox *bBox;
    QSpinBox *hBox;
    QSpinBox *sBox;
    QSpinBox *vBox;
    QSpinBox *aBox;

    QLineEdit *lineEditor;
    QComboBox *comboBox;
    QList<Overlay*> *list;
    QPushButton *confirm;
    QPushButton *reset;
    QPushButton *cancel;

    QPushButton *rgb;
    QPushButton *hsv;
    QPushButton *hex;
    QButtonGroup grp;

    ColorDisplay *display;
    ColorCircle *circle;
    InputCircle *inputCircle;
    
    QColor::Spec spec = QColor::Rgb;
    

    QString colorNameFromSpac(QColor col);
    
    void configureView();
    void configureStylesheet();
    void configureConnections();
    void sliderUpdatesSpinboxWithoutSignal(QSpinBox *, int value);
    void spinboxUpdatesSliderWithoutSignal(QSlider *, int value);
    void colorCircleUpdatesSliderWithoutNotification(QSlider *, QSpinBox *, QColor &);
    void setColorFromHex();
    
    void createOverlay();

    bool isPressed = false;
    QPoint oldPos;
signals:
	void onColorChanged(QColor color);
	void exiting();
public slots:
    
protected:
    void leaveEvent(QEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
};



