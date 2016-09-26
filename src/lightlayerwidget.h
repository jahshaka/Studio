/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef LIGHTLAYERWIDGET_H
#define LIGHTLAYERWIDGET_H

#include <QWidget>
#include "ui_lightlayerwidget.h"

class LightNode;
class LightLayerWidget : public QWidget
{
    Q_OBJECT
    LightNode* light;
public:
    explicit LightLayerWidget(QWidget *parent = 0);
    ~LightLayerWidget();

    void setLight(LightNode* light);

public slots:
    void sliderLightRColor(int val);
    void sliderLightGColor(int val);
    void sliderLightBColor(int val);
    void sliderLightIntensity(int val);
    void sliderLightColor(QColor col);

private:
    Ui::LightLayerWidget *ui;
};

#endif // LIGHTLAYERWIDGET_H
