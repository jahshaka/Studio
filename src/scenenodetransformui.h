/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef NAMEDVALUESLIDER_H
#define NAMEDVALUESLIDER_H

#include <QWidget>
#include "ui_namedvalueslider.h"

class SceneNodeTransformUI:public QWidget
{
public:
    Ui::NamedValueSlider* xposSlider;
    Ui::NamedValueSlider* yposSlider;
    Ui::NamedValueSlider* zposSlider;

    SceneNodeTransformUI(QWidget* parent):
        QWidget(parent)
    {
        QWidget* widget = new QWidget(this);

        xposSlider = new Ui::NamedValueSlider();
        xposSlider->setupUi(widget);

        widget->setGeometry(0,0,300,100);

        widget->show();
    }

};

#endif // NAMEDVALUESLIDER_H
