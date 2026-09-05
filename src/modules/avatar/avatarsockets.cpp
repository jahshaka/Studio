/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "modules/avatar/avatarsockets.h"

#include "irisgl/document/assets/skeleton.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/socket.h"

namespace avatar {
namespace sockets {

namespace {

/// Bone names are compared NORMALIZED, never literally: exporters prefix them
/// with a rig namespace ("mixamorig:Head"), separate words with any of
/// -_./space, and disagree on case. Normalizing to "everything after the last
/// colon, lowercased, letters and digits only" turns `mixamorig:RightShoulder`,
/// `Right_Shoulder` and `rightShoulder` into one string and costs nothing.
QString normalize(const QString &boneName)
{
    const int colon = boneName.lastIndexOf(QLatin1Char(':'));
    const QString tail = colon >= 0 ? boneName.mid(colon + 1) : boneName;
    QString out;
    out.reserve(tail.size());
    for (const QChar c : tail)
        if (c.isLetterOrNumber()) out.append(c.toLower());
    return out;
}

/// Candidates in PRIORITY order — the first one the rig has wins.
///
/// The lists are short on purpose: every entry is a name shape we have actually
/// seen (Mixamo's `mixamorig:` prefix; 3ds Max Biped's `Bip01 Head` /
/// `Bip01 R Clavicle`; VRM's `J_Bip_C_Head`; Maya's `_JNT` suffix; and the
/// bare names glTF exports of all of them produce). Guessing wider — matching
/// any bone whose name CONTAINS "head", say — would happily pick `HeadTop_End`,
/// which is the tip marker above the skull and not where an eye goes.
const QStringList &headCandidates()
{
    static const QStringList list = {
        QStringLiteral("head"),
        QStringLiteral("bip01head"),
        QStringLiteral("jbipchead"),
        QStringLiteral("headjnt"),
        QStringLiteral("headjoint"),
    };
    return list;
}

const QStringList &shoulderCandidates()
{
    static const QStringList list = {
        // Right first: over-the-RIGHT-shoulder is the third-person default
        // everywhere (Unreal's template, Gears/Souls/Witcher convention).
        QStringLiteral("rightshoulder"),
        QStringLiteral("rshoulder"),
        QStringLiteral("shoulderr"),
        QStringLiteral("bip01rclavicle"),
        QStringLiteral("jbiprshoulder"),
        QStringLiteral("rightshoulderjnt"),
        // ...then the left, so a rig that only names one still gets a socket.
        QStringLiteral("leftshoulder"),
        QStringLiteral("lshoulder"),
        QStringLiteral("shoulderl"),
        QStringLiteral("bip01lclavicle"),
        QStringLiteral("jbiplshoulder"),
        QStringLiteral("leftshoulderjnt"),
    };
    return list;
}

const QStringList &candidatesFor(const QString &socketName)
{
    static const QStringList none;
    if (socketName == QLatin1String("head")) return headCandidates();
    if (socketName == QLatin1String("shoulder")) return shoulderCandidates();
    return none;
}

}   // namespace

const QStringList &builtInNames()
{
    static const QStringList list = { QStringLiteral("head"), QStringLiteral("shoulder") };
    return list;
}

QString mapBone(const iris::SkeletonPtr &skeleton, const QString &socketName)
{
    if (skeleton.isNull() || skeleton->bones.isEmpty()) return QString();
    const QStringList &candidates = candidatesFor(socketName);
    if (candidates.isEmpty()) return QString();

    // Normalize the rig ONCE, then walk the candidates in priority order — the
    // other way round (walk bones, score each) makes priority order depend on
    // the rig's bone order, which is arbitrary.
    QHash<QString, QString> byNormalized;
    for (const iris::BonePtr &bone : skeleton->bones) {
        if (bone.isNull()) continue;
        const QString key = normalize(bone->name);
        // First bone wins a collision: `mixamorig:Head` before a stray `head`
        // helper further down the list.
        if (!key.isEmpty() && !byNormalized.contains(key)) byNormalized.insert(key, bone->name);
    }
    for (const QString &candidate : candidates) {
        const auto it = byNormalized.constFind(candidate);
        if (it != byNormalized.constEnd()) return it.value();
    }
    return QString();
}

QVector<Mapping> installBuiltIns(const iris::MeshNodePtr &node)
{
    QVector<Mapping> report;
    const iris::SkeletonPtr skeleton = node.isNull() ? iris::SkeletonPtr() : node->getSkeleton();

    for (const QString &socketName : builtInNames()) {
        Mapping mapping;
        mapping.socket = socketName;
        if (node.isNull()) { report.append(mapping); continue; }

        if (const iris::Socket *existing = node->findSocket(socketName)) {
            // Already there — the user's (or a previous install's) offset stays.
            mapping.existed = true;
            mapping.mapped = true;
            mapping.bone = existing->boneName;
            report.append(mapping);
            continue;
        }

        const QString bone = mapBone(skeleton, socketName);
        if (bone.isEmpty()) { report.append(mapping); continue; }   // fail soft

        iris::Socket socket;
        socket.name = socketName;
        socket.boneName = bone;
        socket.builtIn = true;
        // Identity offset — see the rationale in avatarsockets.h.
        if (node->addSocket(socket)) {
            mapping.mapped = true;
            mapping.bone = bone;
        }
        report.append(mapping);
    }
    return report;
}

}   // namespace sockets
}   // namespace avatar
