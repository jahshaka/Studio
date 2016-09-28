/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef TORUSLAYERWIDGET_H
#define TORUSLAYERWIDGET_H

#include <QWidget>
#include "ui_toruslayerwidget.h"

class TorusNode;
class TorusLayerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TorusLayerWidget(QWidget *parent = 0);
    ~TorusLayerWidget();

    void setTorus(TorusNode* node);
    void initCallbacks();

public slots:
    void sliderTorusMinorRadius(int val);//float
    void sliderTorusRings(int val);
    void sliderTorusSlices(int val);

private:
    Ui::TorusLayerWidget *ui;
    TorusNode* torus;
};

#endif // TORUSLAYERWIDGET_H
