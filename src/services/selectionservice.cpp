/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/selectionservice.h"

#include <algorithm>

#include "irisgl/document/scenegraph/scenenode.h"

namespace {

/// A node's position in the document as the chain of sibling indices from the
/// root down to it. Comparing two chains lexicographically IS document
/// pre-order: an ancestor's chain is a prefix of its descendant's, so it sorts
/// first, and siblings sort by index.
///
/// Built by walking UP (the document has no cheap "index of node in a
/// pre-order walk" and building one would mean a full-scene walk per sort).
QList<int> preOrderKey(const iris::SceneNodePtr &node)
{
    QList<int> key;
    for (iris::SceneNodePtr n = node; !!n; n = n->getParent()) {
        const int idx = n->siblingIndex();
        if (idx < 0) break;                 // the root (or a detached node)
        key.prepend(idx);
    }
    return key;
}

/// A re-entrancy guard that mirrors the pre-set behaviour exactly.
struct Guard
{
    bool &flag;
    explicit Guard(bool &f) : flag(f) { flag = true; }
    ~Guard() { flag = false; }
};

} // namespace

QList<iris::SceneNodePtr> SelectionService::sortDocumentOrder(QList<iris::SceneNodePtr> nodes)
{
    QList<QPair<QList<int>, iris::SceneNodePtr>> keyed;
    keyed.reserve(nodes.size());
    for (const auto &n : nodes) {
        if (!n) continue;
        keyed.append({ preOrderKey(n), n });
    }
    std::stable_sort(keyed.begin(), keyed.end(),
                     [](const QPair<QList<int>, iris::SceneNodePtr> &a,
                        const QPair<QList<int>, iris::SceneNodePtr> &b) {
        return std::lexicographical_compare(a.first.cbegin(), a.first.cend(),
                                            b.first.cbegin(), b.first.cend());
    });
    QList<iris::SceneNodePtr> out;
    out.reserve(keyed.size());
    for (const auto &pair : keyed) out.append(pair.second);
    return out;
}

iris::SceneNodePtr SelectionService::topmost(const QList<iris::SceneNodePtr> &nodes)
{
    const auto ordered = sortDocumentOrder(nodes);
    return ordered.isEmpty() ? iris::SceneNodePtr() : ordered.first();
}

bool SelectionService::isSelected(const iris::SceneNodePtr &node) const
{
    if (!node) return false;
    for (const auto &n : mSelection)
        if (n.data() == node.data()) return true;
    return false;
}

void SelectionService::select(iris::SceneNodePtr node)
{
    if (mInSelect) return;
    Guard guard(mInSelect);

    mSelection.clear();
    if (node) mSelection.append(node);
    // A REPLACE always re-emits, even for the same node: the panels' refresh
    // contract (see the header) predates the set and nothing about a set
    // changes it.
    emit selectionChanged(selected());
    emit selectionSetChanged(mSelection);
}

void SelectionService::select(const QList<iris::SceneNodePtr> &nodes)
{
    if (mInSelect) return;
    Guard guard(mInSelect);

    QList<iris::SceneNodePtr> unique;
    for (const auto &n : nodes) {
        if (!n) continue;
        bool seen = false;
        for (const auto &u : unique) if (u.data() == n.data()) { seen = true; break; }
        if (!seen) unique.append(n);
    }

    mSelection.clear();
    if (!unique.isEmpty()) {
        const iris::SceneNodePtr primary = unique.first();
        QList<iris::SceneNodePtr> tail = unique.mid(1);
        mSelection.append(primary);
        mSelection.append(sortDocumentOrder(tail));
    }
    emit selectionChanged(selected());
    emit selectionSetChanged(mSelection);
}

bool SelectionService::add(iris::SceneNodePtr node)
{
    if (mInSelect || !node) return false;
    if (!mSelection.isEmpty() && mSelection.first().data() == node.data()) return false;
    Guard guard(mInSelect);

    QList<iris::SceneNodePtr> tail;
    for (const auto &n : mSelection)
        if (n.data() != node.data()) tail.append(n);

    mSelection.clear();
    mSelection.append(node);                       // last added = primary
    mSelection.append(sortDocumentOrder(tail));

    emit selectionChanged(selected());             // the primary DID change
    emit selectionSetChanged(mSelection);
    return true;
}

bool SelectionService::remove(iris::SceneNodePtr node)
{
    if (mInSelect || !node) return false;
    if (!isSelected(node)) return false;
    Guard guard(mInSelect);

    const iris::SceneNodePtr oldPrimary = selected();
    QList<iris::SceneNodePtr> rest;
    for (const auto &n : mSelection)
        if (n.data() != node.data()) rest.append(n);

    // D2: removing the primary promotes THE TOPMOST REMAINING row, not the
    // most recently added — the same "topmost" the owner's Shift rule anchors
    // on, so the two never disagree. The tail is already in document order, so
    // this is its head.
    mSelection = sortDocumentOrder(rest);
    if (!mSelection.isEmpty() && oldPrimary.data() != node.data()) {
        // The primary survived: put it back in front.
        QList<iris::SceneNodePtr> reordered;
        reordered.append(oldPrimary);
        for (const auto &n : mSelection)
            if (n.data() != oldPrimary.data()) reordered.append(n);
        mSelection = reordered;
    }

    if (selected().data() != oldPrimary.data()) emit selectionChanged(selected());
    emit selectionSetChanged(mSelection);
    return true;
}

bool SelectionService::toggle(iris::SceneNodePtr node)
{
    if (!node) return false;
    if (isSelected(node)) {
        remove(node);
        return false;
    }
    // Already the primary is impossible here (isSelected covered it), so add
    // always changes something.
    add(node);
    return true;
}

void SelectionService::clear()
{
    if (mInSelect) return;
    if (mSelection.isEmpty()) {
        // Still a replace: keep select(null)'s re-emit contract.
        Guard guard(mInSelect);
        emit selectionChanged(iris::SceneNodePtr());
        emit selectionSetChanged(mSelection);
        return;
    }
    Guard guard(mInSelect);
    mSelection.clear();
    emit selectionChanged(iris::SceneNodePtr());
    emit selectionSetChanged(mSelection);
}
