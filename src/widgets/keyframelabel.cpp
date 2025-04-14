/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "keyframelabel.h"
#include "ui_keyframelabel.h"


void KeyFrameLabel::setKeyFrame(iris::FloatKeyFrame *value)
{
    keyFrame = value;
}

iris::FloatKeyFrame *KeyFrameLabel::getKeyFrame() const
{
    return keyFrame;
}

KeyFrameLabel::KeyFrameLabel(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::KeyFrameLabel)
{
    ui->setupUi(this);

    connect(ui->pushButton, SIGNAL(clicked(bool)), this, SLOT(toggleCollapse()));

    collapsed = false;
}

KeyFrameLabel::~KeyFrameLabel()
{
    delete ui;
}

void KeyFrameLabel::setTitle(QString title)
{
    ui->label->setText(title);
}

void KeyFrameLabel::addChild(KeyFrameLabel *label)
{
    childLabels.append(label);
}

// hides all children and their children
void KeyFrameLabel::collapse()
{
    //expand all children
    for (auto child : childLabels) {
        child->collapse();
        child->hide();
    }
}

// shows all children
void KeyFrameLabel::expand()
{
    //collapse all children
    for (auto child : childLabels) {
        child->show();
        child->expand();
    }
}

void KeyFrameLabel::toggleCollapse()
{
    if(collapsed)
    {
        expand();
        collapsed = false;
    }
    else
    {
        collapse();
        collapsed = true;
    }
}
