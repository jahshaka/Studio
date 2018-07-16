/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

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
    ~MSwitch(){

    }

	void setSizeOfSwitch(int a);

    void setColor(QColor col){
        endColor = col;
    }
    void setOnColor1(QColor col){
        onColor = col;
    }
    QColor onColor1(){
        return onColor.dark(120);
    }

	void toggle() {
		simulateClick();
                emit stateChanged(value());
	}

	void setChecked(bool val){
            if(val){
                if(value()==minimum()) toggle();
            }else{
                if(value()==maximum()) toggle();
            }
	}

	bool isChecked(){
                return value()==maximum() ? true : false;
	}

private:
    bool isOn = false;
    QRect boundingRect;
    QColor startColor, endColor, onColor;
    int wHeight, wWidth, factor=10, offset =3;

	void simulateClick();

protected:
	void paintEvent(QPaintEvent *event);

    void mousePressEvent(QMouseEvent *event){

        Q_UNUSED(event);
        //animate value and color
		simulateClick();
    }

    void mouseReleaseEvent(QMouseEvent *e){
        Q_UNUSED(e);
    }

#if QT_CONFIG(wheelevent)
    void wheelEvent(QWheelEvent *e) Q_DECL_OVERRIDE{
        Q_UNUSED(e);
    }
#endif

signals:
    void switchPressed(bool b);
    void stateChanged(int value);
};

#endif // MSWITCH_H
