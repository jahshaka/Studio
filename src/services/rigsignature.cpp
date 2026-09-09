/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "src/services/rigsignature.h"

#include <QCryptographicHash>

#include "irisgl/document/animation/skeletalanimation.h"

namespace rig {

const double kRigMatchThreshold = 0.5;

bool isPivotChannel(const QString &name)
{
    return name.contains(QStringLiteral("$AssimpFbx$"));
}

bool isJunkClipName(const QString &raw)
{
    static const QSet<QString> junk = {
        QStringLiteral("mixamo.com"), QStringLiteral("take 001"),
        QStringLiteral("default take"), QStringLiteral("unreal take"),
        QStringLiteral("armature|mixamo.com|layer0"), QStringLiteral("animstack::take 001"),
        QStringLiteral("motion"),
    };
    return raw.trimmed().isEmpty() || junk.contains(raw.trimmed().toLower());
}

QString displayNameFor(const QString &rawName, const QString &sourceBaseName)
{
    if (!isJunkClipName(rawName)) return rawName;
    return sourceBaseName.isEmpty() ? QStringLiteral("Clip") : sourceBaseName;
}

QVector<ClipScore> scoreClips(const QMap<QString, iris::SkeletalAnimationPtr> &anims,
                              const QSet<QString> &rigNodeNames)
{
    QVector<ClipScore> scored;
    for (auto it = anims.constBegin(); it != anims.constEnd(); ++it) {
        if (it.value().isNull()) continue;
        ClipScore s;
        s.raw = it.key();
        s.skel = it.value();
        for (auto ch = s.skel->boneAnimations.constBegin();
             ch != s.skel->boneAnimations.constEnd(); ++ch) {
            ++s.channels;
            if (isPivotChannel(ch.key())) continue;
            ++s.boneChannels;
            if (rigNodeNames.contains(ch.key())) ++s.matched;
            else if (s.unmatched.size() < 5) s.unmatched.append(ch.key());
        }
        s.ratio = s.boneChannels > 0 ? double(s.matched) / double(s.boneChannels) : 0.0;
        scored.append(s);
    }
    return scored;
}

int bestClip(const QVector<ClipScore> &scored)
{
    if (scored.isEmpty()) return -1;
    int best = 0;
    for (int i = 1; i < scored.size(); ++i)
        if (scored[i].ratio > scored[best].ratio) best = i;
    return best;
}

QString mismatchMessage(const ClipScore &best, const QString &shownFile, const QString &rigName)
{
    const QString names = best.unmatched.isEmpty()
                              ? QStringLiteral("(pivot channels only)")
                              : best.unmatched.join(QStringLiteral(", "));
    return QStringLiteral("%1 is animating a different rig — %2 of its %3 bones exist in "
                          "'%4' (no match for %5)")
        .arg(shownFile)
        .arg(best.matched)
        .arg(best.boneChannels)
        .arg(rigName, names);
}

QString rigId(const QStringList &boneNames)
{
    // Sorted and de-duplicated: two exports of one skeleton differ in node
    // ORDER all the time (assimp walks the file's hierarchy), never in the set
    // of bone names — so the set is the identity and the order is noise.
    QStringList sorted;
    QSet<QString> seen;
    for (const QString &name : boneNames) {
        const QString trimmed = name.trimmed();
        if (trimmed.isEmpty() || seen.contains(trimmed)) continue;
        seen.insert(trimmed);
        sorted.append(trimmed);
    }
    if (sorted.isEmpty()) return QString();
    sorted.sort();
    QCryptographicHash hash(QCryptographicHash::Sha1);
    for (const QString &name : sorted) {
        hash.addData(name.toUtf8());
        hash.addData(QByteArrayLiteral("\n"));   // a separator, or "ab"+"c" == "a"+"bc"
    }
    return QString::fromLatin1(hash.result().toHex());
}

}   // namespace rig
