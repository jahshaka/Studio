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
#include <QLayoutItem>
#include <QScrollBar>
#include <vector>
//#include "../scenegraph/scenenodes.h"
#include "../irisgl/src/scenegraph/scenenode.h"
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
    if (!!obj && obj->hasActiveAnimation()) {
        //this->setKeyFrameSet(obj->animation->keyFrameSet);
    } else {
        this->clearKeyFrameSet();
    }
}

void KeyFrameLabelWidget::setKeyFrameSet(iris::KeyFrameSetPtr frameSet)
{
    //foreach (QWidget * w, ui->scrollAreaWidgetContents->findChildren<QWidget*>()) delete w;
    //delete ui->scrollAreaWidgetContents->layout();
    clearLayout(ui->scrollAreaWidgetContents->layout());
    delete ui->scrollAreaWidgetContents->layout();
    labels.clear();

    auto layout = new QVBoxLayout();
    layout->setSpacing(0);
    layout->setContentsMargins(0, 0, 0, 0);

    //todo: group widgets
    for (auto key : frameSet->keyFrames.keys()) {
        auto frame = frameSet->keyFrames[key];

        auto label = new KeyFrameLabel();
        label->setTitle(key);
        //label->setKeyFrame(frame);
        layout->addWidget(label);
        labels.append(label);
    }

    layout->addStretch();
    ui->scrollAreaWidgetContents->setLayout(layout);
}

void KeyFrameLabelWidget::clearKeyFrameSet()
{
    //delete ui->scrollAreaWidgetContents->layout();
    //foreach (QWidget * w, ui->scrollAreaWidgetContents->findChildren<QWidget*>()) delete w;
    clearLayout(ui->scrollAreaWidgetContents->layout());
    labels.clear();
}

void KeyFrameLabelWidget::resetKeyFrames()
{
    if (!!obj && obj->hasActiveAnimation()) {
        //this->setKeyFrameSet(obj->animation->keyFrameSet);
    }
}

void KeyFrameLabelWidget::scrollValueChanged(int val)
{
    //qDebug() << "Scroll value: " << val;
    //dopeSheet->setScrollValue(val);
}

void KeyFrameLabelWidget::clearLayout(QLayout *layout)
{
    if (layout == nullptr)
        return;

    QLayoutItem* item;
    while ( ( item = layout->takeAt(0) ) != nullptr ){
        delete item->widget();
        delete item;
    }
}

QList<KeyFrameLabel *> KeyFrameLabelWidget::getLabels() const
{
    return labels;
}

bool KeyFrameLabelWidget::eventFilter(QObject *obj, QEvent *evt)
{
    if (evt->type() == QEvent::Resize) {

        auto resizeEvt = static_cast<QResizeEvent*>(evt);
        //qDebug() << resizeEvt->size();
        /*
        if (dopeSheet != nullptr) {
           dopeSheet->setContentHeight(resizeEvt->size().height());
        }
        */
        return true;
    }

    return false;
}


