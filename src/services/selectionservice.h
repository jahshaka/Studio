/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SELECTIONSERVICE_H
#define SELECTIONSERVICE_H

// SelectionService — the editor selection (APP_ARCHITECTURE_AUDIT §3.3).
//
// Owns what used to be MainWindow's private activeSceneNode plus the
// re-entrancy guard (a viewport may echo setSelectedNode() through
// EditorViewportEvents::sceneNodeSelected, which lands back in select()).
// The widget fan-out (viewport, hierarchy, properties, timeline) is the
// shell's business: it connects to selectionChanged().
//
// Note select(sameNode) deliberately re-emits: the pre-extraction behaviour
// re-ran the fan-out on re-selection (e.g. after toggling play mode) and the
// panels rely on that refresh.

#include <QObject>

#include "../../irisgl/src/irisglfwd.h"

class SelectionService : public QObject
{
    Q_OBJECT

public:
    explicit SelectionService(QObject *parent = nullptr) : QObject(parent) {}

    /// Selects a node (null deselects) and notifies. Re-entrant calls made
    /// from within the fan-out are ignored, exactly like the old guard.
    void select(iris::SceneNodePtr node);

    iris::SceneNodePtr selected() const { return mSelected; }

signals:
    void selectionChanged(iris::SceneNodePtr node);

private:
    iris::SceneNodePtr mSelected;
    bool mInSelect = false;
};

#endif // SELECTIONSERVICE_H
