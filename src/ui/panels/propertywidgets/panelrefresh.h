#pragma once
// panelrefresh::afterFrames — A PANEL'S READ-BACK AFTER AN EDIT, IN FRAMES (ENGINE-UPGRADE-U3 stage 2).
//
// The World/Camera panels used to render two frames inline after every edit (`renderFrames(2)`) so their read-only
// rows showed what the renderer did with it: an enable-flag change is a workspace rebuild the mirror applies at the
// next sync, and one frame can be the one that rebuilds rather than the one that draws with it. Those renders are
// deleted — the panel reads the published snapshot, and it re-reads it after the next `frames` frames of its viewport
// close (EditorViewportEvents::framesDrawn: the driver's ticks, or a script's lockstep frames). Never a timer.
#include <QObject>
#include <QPointer>
#include <functional>
#include <memory>

#include "viewport/ieditorviewport.h"

namespace panelrefresh {

inline void afterFrames(QObject *owner, IEditorViewport *viewport, int frames, std::function<void()> refresh)
{
    if (!owner || !viewport || frames <= 0) { if (refresh) refresh(); return; }
    auto left = std::make_shared<int>(frames);
    auto conn = std::make_shared<QMetaObject::Connection>();
    EditorViewportEvents *events = viewport->events();
    if (!events) { if (refresh) refresh(); return; }
    *conn = QObject::connect(events, &EditorViewportEvents::framesDrawn, owner, [left, conn, refresh]() {
        if (--*left > 0) return;
        QObject::disconnect(*conn);
        if (refresh) refresh();
    });
}

}   // namespace panelrefresh
