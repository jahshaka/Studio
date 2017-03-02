/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include <QWidget>
#include <QDebug>
#include <QPainter>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <vector>
//#include "../scenegraph/scenenodes.h"
#include "../irisgl/src/core/scenenode.h"
#include "../irisgl/src/animation/keyframeset.h"
#include "../irisgl/src/animation/keyframeanimation.h"
#include "../irisgl/src/animation/animation.h"
#include "keyframelabelwidget.h"
#include "keyframelabel.h"
#include "ui_keyframelabelwidget.h"


KeyFrameLabelWidget::KeyFrameLabelWidget(QWidget* parent):
    QWidget(parent),
    ui(new Ui::KeyFrameLabelWidget)
{
    ui->setupUi(this);

    bgColor = QColor::fromRgb(50,50,50);
    itemColor = QColor::fromRgb(255,255,255);

    cursorPen = QPen(QColor::fromRgb(255,255,255));
    cursorPen.setWidth(3);

    auto scrollBar = ui->scrollArea->verticalScrollBar();
    //connect(scrollBar,SIGNAL(valueChanged(int)), this, SLOT(scrollValueChanged(int)));

    ui->scrollAreaWidgetContents->installEventFilter(this);
}

void KeyFrameLabelWidget::setSceneNode(iris::SceneNodePtr node)
{
    obj = node;
    if (!!obj->animation) {
        this->setKeyFrameSet(obj->animation->keyFrameSet);
    } else {
        this->clearKeyFrameSet();
    }
}

void KeyFrameLabelWidget::setKeyFrameSet(iris::KeyFrameSetPtr frameSet)
{
    auto layout = new QVBoxLayout();
    ui->scrollAreaWidgetContents->setLayout(layout);

    //todo: group widgets
    for (auto frame : frameSet->keyFrames) {

    }
}

void KeyFrameLabelWidget::clearKeyFrameSet()
{
    auto layout = new QVBoxLayout();
    ui->scrollAreaWidgetContents->setLayout(layout);
}

void KeyFrameLabelWidget::scrollValueChanged(int val)
{
    qDebug() << "Scroll value: " << val;
    //dopeSheet->setScrollValue(val);
}

bool KeyFrameLabelWidget::eventFilter(QObject *obj, QEvent *evt)
{
    if (evt->type() == QEvent::Resize) {

        auto resizeEvt = static_cast<QResizeEvent*>(evt);
        qDebug() << resizeEvt->size();

//        if (dopeSheet != nullptr) {
//           dopeSheet->setContentHeight(resizeEvt->size().height());
//        }

        return true;
    }

    return false;
}


