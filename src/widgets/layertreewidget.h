/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef LAYERTREE_H
#define LAYERTREE_H

#include <QTreeWidget>
#include <QDropEvent>
#include <QDebug>
//#include "../scenegraph/scenenodes.h"
#include "../irisgl/src/core/scenenode.h"

//class QTreeWidget;

class LayerTreeWidget:public QTreeWidget
{
public:
    LayerTreeWidget(QWidget* parent):
        QTreeWidget(parent)
    {

    }

    virtual void dropEvent(QDropEvent* event)
    {
        Q_UNUSED(event);
        //QTreeWidget::dropEvent(event);
        /*
        //qDebug()<<"drop event";
        //event->accept();
        auto source = reinterpret_cast<QTreeWidgetItem*>(event->source());//source is widget
        Q_ASSERT(source != nullptr);

        auto target = itemAt(event->pos());
        Q_ASSERT(target != nullptr);

        auto sourceNode = (SceneNode*)source->data(1,Qt::UserRole).value<void*>();
        auto targetNode = (SceneNode*)target->data(1,Qt::UserRole).value<void*>();
        */
    }
};

#endif // LAYERTREE_H
