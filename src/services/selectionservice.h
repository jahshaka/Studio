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
// A SET WITH A PRIMARY since EDITOR_MULTISELECT_SPEC §2.1. The selection is an
// ordered list whose FIRST entry is the primary — the node that drives the
// properties panel, the gizmo pivot and every single-target verb — and whose
// tail is kept in document pre-order. `selected()` still answers the primary
// and `select(node)` still replaces the selection with one node, so every
// caller written against the single-selection service keeps working unchanged.
//
// Two signals, deliberately different in when they fire:
//   selectionChanged(primary)    — the PRIMARY changed (or a replace-select
//                                  ran, which re-emits even for the same node:
//                                  the pre-extraction behaviour re-ran the
//                                  panel fan-out on re-selection and the panels
//                                  rely on that refresh).
//   selectionSetChanged(set)     — the set changed in any way. A Ctrl+click
//                                  storm that never touches the primary must
//                                  not rebuild the properties panel N times
//                                  (EDITOR_MULTISELECT_SPEC §3.3), which is
//                                  exactly why add/toggle/remove keep the
//                                  primary signal quiet when the primary is
//                                  unchanged.
//
// The ordering helpers are static and public because the verbs and the tree
// need the same document order the service stores.

#include <QList>
#include <QObject>

#include "irisgl/irisglfwd.h"

class SelectionService : public QObject
{
    Q_OBJECT

public:
    explicit SelectionService(QObject *parent = nullptr) : QObject(parent) {}

    /// Selects a node (null deselects) and notifies. Re-entrant calls made
    /// from within the fan-out are ignored, exactly like the old guard.
    void select(iris::SceneNodePtr node);

    /// Replaces the selection with a SET — the first entry is the primary, the
    /// rest are stored in document pre-order. Nulls and duplicates are dropped.
    void select(const QList<iris::SceneNodePtr> &nodes);

    /// Adds a node and makes it the primary. False when the node is null or
    /// was already the primary (nothing changed).
    bool add(iris::SceneNodePtr node);

    /// Adds or removes; returns the node's membership AFTER the call.
    /// Removing the primary promotes the topmost remaining member (D2).
    bool toggle(iris::SceneNodePtr node);

    /// Drops a node from the set (promotes on primary removal). False when it
    /// was not a member.
    bool remove(iris::SceneNodePtr node);

    void clear();

    /// The primary — the node every single-target consumer acts on.
    iris::SceneNodePtr selected() const
    {
        return mSelection.isEmpty() ? iris::SceneNodePtr() : mSelection.first();
    }
    /// The whole selection, primary first.
    QList<iris::SceneNodePtr> selectedSet() const { return mSelection; }
    int count() const { return mSelection.size(); }
    bool isSelected(const iris::SceneNodePtr &node) const;

    /// The list in DOCUMENT PRE-ORDER (the order the scene writer walks): an
    /// ancestor before its descendants, siblings by index. Used for the set's
    /// tail, the verbs' return values and "topmost".
    static QList<iris::SceneNodePtr> sortDocumentOrder(QList<iris::SceneNodePtr> nodes);
    /// The first node of `nodes` in document pre-order (null for an empty list).
    static iris::SceneNodePtr topmost(const QList<iris::SceneNodePtr> &nodes);

signals:
    void selectionChanged(iris::SceneNodePtr node);
    void selectionSetChanged(const QList<iris::SceneNodePtr> &nodes);

private:
    /// The set, primary FIRST, tail in document pre-order.
    QList<iris::SceneNodePtr> mSelection;
    bool mInSelect = false;
};

#endif // SELECTIONSERVICE_H
