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
// (tests/services/test_editpose.cpp), the nodes given as the document's guid -> node map. A node needs `guid` (a
// UTF-8 std::string), getLocalPos/Rot/Scale and setLocalPos/Rot/Scale;
// a snapshot entry needs `pos`, `rot`, `scale`.
//
// ONLY A NODE WHOSE POSE DIFFERS IS WRITTEN: a transform write marks the node moved, and on a STATIC node that
// demotes it (irisgl nodegraph.cpp markMoved) — a node Play moved was demoted by its own motion already, one it
// never moved is not touched. The writes reach the engine only as the document's dirty marks, read at the
// mirror's next frame sync; swap and put-back happen with no frame between, so that sync sees the play pose it
// already had.

#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QVector>

#include <initializer_list>

namespace editpose {

/// What a save swaps in for its walk (documentEditPose): the edit pose by guid.
template <class Pose>
struct EditPose
{
    QMap<QString, Pose> pose;
    /// A RUN's play-start snapshot holds every node the document had when it started, so a node outside it was added
    /// since (the save leaves it out). A simulation's holds only its bodies: a node outside it did not move.
    bool wholeDocument = false;
};

/// THE DOCUMENT'S EDIT POSE (STUDIO-HYGIENE-1 final read B1) — the one answer every save asks. THREE things move a
/// document's nodes: the editor's play-in-place, the Player page's run (each a PlayBack bound to the same document),
/// and Simulate Physics (the physics world stepping with no run). Two runs can play at once (editor.play, then the
/// Player page: nothing stops the editor's run), and the second one's snapshot is the first one's PLAY pose — so the
/// answer is the EARLIEST-started run playing on `document` (the lowest playSerial). With no run, a simulation's
/// held body poses (`simulated`, the physics environment's; empty when none runs). A Run needs isScenePlaying(),
/// playSerial(), document() and playStartPose(); a held entry needs pos, rot, scale.
template <class Pose, class Run, class Doc, class Held>
EditPose<Pose> documentEditPose(std::initializer_list<const Run *> runs, const Doc &document, const Held *simulated)
{
    EditPose<Pose> edit;
    const Run *first = nullptr;
    for (const Run *run : runs)
        if (run && run->isScenePlaying() && run->document() == document
            && (!first || run->playSerial() < first->playSerial()))
            first = run;
    if (first) {
        edit.pose = first->playStartPose();
        edit.wholeDocument = true;
        return edit;
    }
    if (simulated)
        for (auto it = simulated->constBegin(); it != simulated->constEnd(); ++it) {
            Pose p;
            p.pos = it->pos;
            p.rot = it->rot;
            p.scale = it->scale;
            edit.pose.insert(it.key(), p);
        }
    return edit;
}

/// A RUN's PLAY-START SNAPSHOT: every node's local pose, except a node a simulation already running has moved
/// (Simulate Physics pressed before Play): its pose before that simulation, `held` (the environment's), is the edit
/// pose — the run's start re-builds the physics world and overwrites the environment's copy with the moved pose.
template <class Pose, class NodeMap, class Held>
QMap<QString, Pose> playStartSnapshot(const NodeMap &nodes, const Held &held)
{
    QMap<QString, Pose> snapshot;
    for (const auto &[nodeKey, node] : nodes) {   // the document's guid -> node map (UTF-8 keys)
        if (!node) continue;
        const QString guid = QString::fromStdString(node->guid);   // the editor's keys stay QString (its edge)
        Pose p;
        const auto h = held.constFind(guid);
        if (h != held.constEnd()) {
            p.pos = h->pos;
            p.rot = h->rot;
            p.scale = h->scale;
        } else {
            p.pos = node->getLocalPos();
            p.rot = node->getLocalRot();
            p.scale = node->getLocalScale();
        }
        snapshot.insert(guid, p);
    }
    return snapshot;
}

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
template <class NodeMap, class Snapshot>
QVector<Played<typename NodeMap::mapped_type, typename Snapshot::mapped_type>>
swapIn(const NodeMap &nodes, const Snapshot &snapshot, QSet<QString> *spawned)
{
    QVector<Played<typename NodeMap::mapped_type, typename Snapshot::mapped_type>> played;
    for (const auto &[nodeKey, node] : nodes) {   // the document's guid -> node map; each put back alone (no order)
        if (!node) continue;
        const QString guid = QString::fromStdString(node->guid);
        const auto it = snapshot.constFind(guid);
        if (it == snapshot.constEnd()) {
            if (spawned) spawned->insert(guid);
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
