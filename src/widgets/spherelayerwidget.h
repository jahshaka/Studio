/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#ifndef SPHERELAYERWIDGET_H
#define SPHERELAYERWIDGET_H

#include <QWidget>
#include "ui_spherelayerwidget.h"

namespace Ui {
class SphereLayerWidget;
}

class SphereNode;

class SphereLayerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SphereLayerWidget(QWidget *parent = 0);
    ~SphereLayerWidget();

    void setSphere(SphereNode* node);
    void initCallbacks();

public slots:
    void sliderRings(int val);
    void sliderSlices(int val);

private:
    Ui::SphereLayerWidget *ui;

    SphereNode* sphere;
};

#endif // SPHERELAYERWIDGET_H
