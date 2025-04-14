/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef BASEWIDGET_H
#define BASEWIDGET_H

#include <QWidget>

enum WidgetType
{
    IntWidget,
    FloatWidget,
    BoolWidget,
    ColorWidget,
    TextureWidget,
    FileWidget
};

class BaseWidget : public QWidget
{
public:
    BaseWidget(QWidget *parent = nullptr) : QWidget(parent) {

    }

    // unsigned id;
    WidgetType type;
};

#endif // BASEWIDGET_H
