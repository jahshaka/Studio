/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef EDITPOSE_H
#define EDITPOSE_H

// THE EDIT POSE, FOR A SAVE MADE DURING PLAY (STUDIO-HYGIENE-1 audit, the lead's option A).
//
// Play moves the document's own nodes, and PlayBack keeps every node's pre-play local transform by guid
// (PlayBack::saveNodeTransforms). A save during Play must write the scene the user EDITED, never the pose the
// simulation left it in: the save door swaps the snapshot in for the walk and puts the play pose back in the
// same UI-thread call (ProjectService::save, behind a scope guard on every exit).
//
// Pure over the snapshot: templated on the node so a CPU suite drives it with a stand-in node
// (tests/services/test_editpose.cpp). A node needs `guid`, getLocalPos/Rot/Scale and setLocalPos/Rot/Scale;
// a snapshot entry needs `pos`, `rot`, `scale`.
//
// ONLY A NODE WHOSE POSE DIFFERS IS WRITTEN: a transform write marks the node moved, and on a STATIC node that
// demotes it (irisgl nodegraph.cpp markMoved) — a node Play moved was demoted by its own motion already, one it
// never moved is not touched. The writes reach the engine only as the document's dirty marks, read at the
// mirror's next frame sync; swap and put-back happen with no frame between, so that sync sees the play pose it
// already had.

#include <QList>
#include <QSet>
#include <QString>
#include <QVector>

namespace editpose {

/// A node the swap wrote, with the play pose it puts back.
template <class NodePtr, class Pose>
struct Played
{
    NodePtr node;
    Pose pose;
};

/// Writes the snapshot's pose into every node of `nodes` that has one and stands elsewhere; returns what to put
/// back. A node with NO snapshot entry was spawned during Play: its guid goes to `spawned` (the save leaves it
/// out — the play-start set is the document).
template <class NodePtr, class Snapshot>
QVector<Played<NodePtr, typename Snapshot::mapped_type>>
swapIn(const QList<NodePtr> &nodes, const Snapshot &snapshot, QSet<QString> *spawned)
{
    QVector<Played<NodePtr, typename Snapshot::mapped_type>> played;
    for (const NodePtr &node : nodes) {
        if (!node) continue;
        const auto it = snapshot.constFind(node->guid);
        if (it == snapshot.constEnd()) {
            if (spawned) spawned->insert(node->guid);
            continue;
        }
        typename Snapshot::mapped_type now;
        now.pos = node->getLocalPos();
        now.rot = node->getLocalRot();
        now.scale = node->getLocalScale();
        if (now.pos == it->pos && now.rot == it->rot && now.scale == it->scale) continue;
        played.append({ node, now });
        node->setLocalPos(it->pos);
        node->setLocalRot(it->rot);
        node->setLocalScale(it->scale);
    }
    return played;
}

/// Puts the play pose back, exactly as swapIn read it.
template <class Played>
void putBack(const QVector<Played> &played)
{
    for (const auto &p : played) {
        p.node->setLocalPos(p.pose.pos);
        p.node->setLocalRot(p.pose.rot);
        p.node->setLocalScale(p.pose.scale);
    }
}

}   // namespace editpose

#endif // EDITPOSE_H
