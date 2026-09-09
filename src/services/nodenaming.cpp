/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/nodenaming.h"

#include <QSet>

#include "irisgl/document/scenegraph/scenenode.h"

namespace nodenaming {

void splitNumericSuffix(const QString &name, QString &stem, int &number)
{
    int end = name.size();
    while (end > 0 && name.at(end - 1).isDigit()) --end;
    stem = name.left(end);
    number = 0;
    if (end < name.size()) {
        bool ok = false;
        // An absurdly long digit run (or one past INT_MAX) is not a count —
        // treat it as part of the stem rather than silently renumbering from 0.
        const int parsed = name.mid(end).toInt(&ok);
        if (ok) number = parsed;
        else    { stem = name; number = 0; }
    }
}

QString uniqueSiblingName(const iris::SceneNodePtr &parent, const QString &desired,
                          const iris::SceneNode *except)
{
    if (!parent) return desired;

    QSet<QString> taken;
    for (const auto &child : parent->children())
        if (!!child && child.data() != except) taken.insert(child->getName());

    // NOT TAKEN = NOT RENAMED. A paste into a parent that has no node of this
    // name keeps the name; the suffix disambiguates, it does not brand.
    if (!taken.contains(desired)) return desired;

    QString stem;
    int number = 0;
    splitNumericSuffix(desired, stem, number);
    // "Cube" (no number) becomes "Cube2", not "Cube1": the original IS the
    // first one, so the copy is the second.
    int next = qMax(2, number + 1);
    // Terminates: `next` only ever grows and `taken` is finite.
    QString candidate = stem + QString::number(next);
    while (taken.contains(candidate)) candidate = stem + QString::number(++next);
    return candidate;
}

} // namespace nodenaming
