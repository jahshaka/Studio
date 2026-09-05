/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef AVATARSOCKETS_H
#define AVATARSOCKETS_H

// The avatar module's BUILT-IN sockets (CAMERAS_SPEC §5, owner decision D9).
//
// Sockets themselves are generic and live in the document (irisgl
// document/scenegraph/socket.h). What lives HERE is the one thing that is
// avatar-specific and has no business in IrisGL: knowing that a bone called
// `mixamorig:Head` and a bone called `Bip01_Head` and a bone called `head` are
// all the same joint. That is a content convention, it changes as rig exporters
// change, and the avatar module is where the rest of that knowledge already
// sits (clip-name junk lists, the pivot-node rules, the bone-tree derivation).
//
// TWO built-ins, per the owner decision: `head` (first person — the camera an
// agent driving the avatar sees through) and `shoulder` (third person). The
// third-person convention is the RIGHT shoulder, falling back to the left.
//
// THE OFFSETS ARE IDENTITY, deliberately. A socket's offset is expressed in
// BONE space, and neither the bone axes nor the world scale are constant across
// the rigs we support: a Mixamo character is ~170 units tall with the head bone
// running along local +Y, a glTF export of the same character is ~1.7 units
// tall, and other exporters orient bones differently again. Any "back 40 cm and
// up 15 cm" this file could hard-code would be right for one file and wrong for
// the next, and wrong SILENTLY. So the built-ins deliver the thing that IS
// reliable — the joint each socket names — and the framing offset stays
// authored, per character, through node.addSocket / the socket's own offset.
//
// FAIL SOFT (the brief's word): a rig with no recognizable head gets no head
// socket and no error. What the caller gets back is a REPORT — which socket
// mapped to which bone, and which did not map at all — so the page and the
// verbs can say "this rig has no shoulder" instead of pretending.

#include <QString>
#include <QStringList>
#include <QVector>

#include "irisgl/irisglfwd.h"

namespace avatar {
namespace sockets {

/// One built-in's outcome.
struct Mapping
{
    QString socket;    ///< "head" | "shoulder"
    QString bone;      ///< the bone it mapped to; empty when unmapped
    bool    mapped = false;
    bool    existed = false;   ///< the node already had a socket of this name (left alone)
};

/// The socket names this module installs, in install order.
const QStringList &builtInNames();

/// The bone this rig uses for `socketName` ("head" | "shoulder"), or an empty
/// string when the rig has none. Public so the suites can assert the mapping
/// table directly, without a node.
QString mapBone(const iris::SkeletonPtr &skeleton, const QString &socketName);

/// Installs the built-ins on a rigged mesh node, returning one Mapping per
/// built-in. A socket the node already has is LEFT ALONE (`existed`), never
/// overwritten: the user's authored offset outlives a re-install.
/// An unrigged node returns two unmapped rows and changes nothing.
QVector<Mapping> installBuiltIns(const iris::MeshNodePtr &node);

}   // namespace sockets
}   // namespace avatar

#endif // AVATARSOCKETS_H
