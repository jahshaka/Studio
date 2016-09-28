/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "surfaceview.h"

#include "QWindow"
#include "QScreen"
#include "QKeyEvent"
#include "QOpenGLContext"
#include "QGuiApplication"
#include <QEvent>

#include "../editor/editorcameracontroller.h"

SurfaceView::SurfaceView(QScreen* screen):
        QWindow(screen)
{
    setSurfaceType(QSurface::OpenGLSurface);

    dragging = false;
    cameraController = nullptr;

    this->installEventFilter(this);


    resize(600, 480);

    QSurfaceFormat format;
    if (QOpenGLContext::openGLModuleType() == QOpenGLContext::LibGL) {
        format.setVersion(4, 3);
        format.setProfile(QSurfaceFormat::CoreProfile);
    }
    format.setDepthBufferSize( 24 );
    //format.setSamples( 4 );
    format.setStencilBufferSize(8);
    setFormat(format);
    create();
}

void SurfaceView::keyPressEvent(QKeyEvent *e)
{
    switch ( e->key() )
    {
        case Qt::Key_Escape:
            QGuiApplication::quit();
            break;

        default:
            QWindow::keyPressEvent( e );
    }
}

//http://www.informit.com/articles/article.aspx?p=1405544&seqNum=2
bool SurfaceView::eventFilter(QObject *obj, QEvent *event)
{
    QEvent::Type type = event->type();
    if(type == QEvent::MouseMove)
    {
        QMouseEvent* evt = static_cast<QMouseEvent*>(event);
        mouseMoveEvent(evt);
        return true;
    }
    else if(type == QEvent::MouseButtonPress)
    {
        QMouseEvent* evt = static_cast<QMouseEvent*>(event);
        mousePressEvent(evt);
        return true;
    }
    else if( type == QEvent::MouseButtonRelease)
    {
        QMouseEvent* evt = static_cast<QMouseEvent*>(event);
        mouseReleaseEvent(evt);
        return true;
    }

    return QWindow::eventFilter(obj,event);
}

void SurfaceView::mouseMoveEvent(QMouseEvent *e)
{
    QPointF localPos = e->localPos();

    if(dragging && cameraController!=nullptr)
    {
        //QPointF dir = localPos-prevPos;
        //cameraController->rotate(dir.x(),dir.y());
    }

    prevPos= localPos;
}

void SurfaceView::mousePressEvent(QMouseEvent *e)
{
    if(e->button()== Qt::RightButton)
    {
        dragging = true;
    }
}

void SurfaceView::mouseReleaseEvent(QMouseEvent *e)
{
    if(e->button()== Qt::RightButton)
    {
        dragging = false;
    }
}

SurfaceView::~SurfaceView()
{

}
