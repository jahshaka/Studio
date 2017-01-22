/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef SURFACEVIEW_H
#define SURFACEVIEW_H

#include "QWindow"
#include <QDropEvent>
#include <QMimeData>

class QScreen;
class QEvent;

class EditorCameraController;

class SurfaceView:public QWindow
{
    Q_OBJECT

    //previous mouse position
    QPointF prevPos;
    bool dragging;

    EditorCameraController* cameraController;
public:
    explicit SurfaceView(QScreen* screen = 0);
    virtual void keyPressEvent(QKeyEvent *e);

protected:
    virtual bool eventFilter(QObject *obj, QEvent *event) Q_DECL_OVERRIDE;

    //http://stackoverflow.com/questions/18559791/mouse-events-in-qt
    //http://stackoverflow.com/questions/18551387/tracking-mouse-coordinates-in-qt/18552004#18552004
    void mouseMoveEvent(QMouseEvent *e);
    void mousePressEvent(QMouseEvent *e);
    void mouseReleaseEvent(QMouseEvent *e);

    ~SurfaceView();
};

#endif // SURFACEVIEW_H
