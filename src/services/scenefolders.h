/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENEFOLDERS_H
#define SCENEFOLDERS_H

// Outliner folders (SPECS/SCENEGRAPH_SPEC.md §6b) — ONE file owns the policy.
//
// A folder is EDITOR ORGANISATION and nothing else. It is not a node: it has no
// transform, no place in the scene hierarchy, no guid, and the player, the
// exporters and the engine never see one. Two pieces of state carry all of it:
//
//   * iris::SceneNode::folderPath — "Props/Kitchen", empty = the root level;
//   * iris::Scene::folders        — the folders that EXIST, which is what lets
//                                   an empty folder survive (membership alone
//                                   cannot express one). Unreal's model.
//
// The transform tree and the folder tree are ORTHOGONAL. Filing a node in a
// folder NEVER reparents it — that is the law of §6b, and the reason drag-to-
// folder and drag-to-reparent can share one tree without fighting: dropping on
// a node row reparents, dropping on a folder row files. Folders organise what a
// level of the outliner SHOWS, and v1 scopes that to the root level (a node
// inside a real parent chain displays under its parent, exactly as today).
//
// Everything here is a pure document edit. Undo rides Snapshot/restore below —
// folder state is small and entirely metadata, so a whole-state snapshot is
// both simpler and more obviously correct than per-operation inverses (renaming
// a folder re-paths every descendant path and every member; removing one moves
// members AND sub-folders up a level).

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "irisgl/irisglfwd.h"

namespace scenefolders {

/// The whole folder state of a scene, enough to restore it exactly.
struct Snapshot
{
    QStringList folders;                 ///< the explicit list, normalised
    QHash<QString, QString> membership;  ///< node guid -> folderPath (only non-empty ones)
};

/// Trims, collapses repeated separators, drops empty segments and leading /
/// trailing slashes. Backslashes are accepted as separators (a path typed on
/// Windows habits still lands in the right place). Returns "" for a path that
/// normalises to nothing, which is the ROOT level.
QString normalize(const QString &path);

/// The path's parent ("Props/Kitchen" -> "Props", "Props" -> ""). Already
/// normalised input assumed.
QString parentOf(const QString &path);
/// The last segment ("Props/Kitchen" -> "Kitchen").
QString leafOf(const QString &path);

/// Every folder in the scene, sorted, with every ANCESTOR present: the explicit
/// list plus every path implied by a node's membership. This is what the panel
/// renders and what scene.folders() reports.
QStringList all(const iris::ScenePtr &scene);

/// True when `path` is a folder of this scene (or is the root level, "").
bool exists(const iris::ScenePtr &scene, const QString &path);

/// Creates the folder (and its ancestors). False when the path normalises to
/// nothing or the folder already exists.
bool create(const iris::ScenePtr &scene, const QString &path);

/// Files one node. An empty path moves it back to the root level. The folder is
/// created if it does not exist yet, so this verb never fails on a fresh path.
/// The node is NOT reparented — see the header note. False only when the node
/// is null.
bool setNodeFolder(const iris::ScenePtr &scene, const iris::SceneNodePtr &node,
                   const QString &path);

/// Renames the LEAF of `path` to `newLeaf`, re-pathing every sub-folder and
/// every member. False when the path is not a folder, the new leaf is empty or
/// contains a separator, or the destination already exists.
bool rename(const iris::ScenePtr &scene, const QString &path, const QString &newLeaf);

/// Removes the folder. Its members and sub-folders MOVE UP to its parent path —
/// deleting a folder never deletes a node (§6b). False when the path is not a
/// folder.
bool remove(const iris::ScenePtr &scene, const QString &path);

/// Normalises the scene's explicit folder list: every entry normalised, every
/// ancestor present, no duplicates, sorted. Run after every edit.
///
/// It deliberately does NOT drop folders that nobody is in. Filing a node in a
/// path makes that path explicit, and it STAYS explicit when the last member
/// leaves — dragging the last thing out of a folder leaves the empty folder
/// behind, which is what Unreal does and what "an explicit persisted list for
/// empty folders" (§6b) means. Removing a folder is an explicit gesture.
void normalizeList(const iris::ScenePtr &scene);

/// The nodes filed under exactly `path` (not its sub-folders), in tree order.
QList<iris::SceneNodePtr> membersOf(const iris::ScenePtr &scene, const QString &path);

// ---- undo + persistence ----------------------------------------------------

Snapshot snapshot(const iris::ScenePtr &scene);
void restore(const iris::ScenePtr &scene, const Snapshot &snap);

/// Writes the folder block into the project's EDITOR object (beside
/// editor.camera). Deliberately NOT part of the node format: folders are editor
/// metadata, and the scene format has no business carrying them.
void writeEditorBlock(QJsonObject &editorObj, const iris::ScenePtr &scene);
/// Reads it back onto the scene and its nodes. Tolerates a missing block (every
/// project written before folders existed).
void readEditorBlock(const QJsonObject &editorObj, const iris::ScenePtr &scene);

}   // namespace scenefolders

// ---------------------------------------------------------------------------
// Implementation. HEADER-ONLY on purpose: src/io/scenewriter.cpp and
// scenereader.cpp call the two persistence helpers, and those two translation
// units are compiled directly into FOURTEEN separate test executables. A .cpp
// here would mean adding it to every one of their CMakeLists for four lines of
// JSON — the dependency, not the code, is what makes this inline.
// ---------------------------------------------------------------------------

#include <QJsonArray>
#include <QSet>
#include <functional>

#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"

namespace scenefolders {
namespace detail {

/// Every node in the scene, depth-first from the root, INCLUDING the root's own
/// children only-by-walking (childAt is sparse — the engine's own helper nodes
/// share the tree and answer null).
inline void walk(const iris::SceneNodePtr &node, const std::function<void(const iris::SceneNodePtr &)> &fn)
{
    if (!node) return;
    fn(node);
    const int kids = node->childCount();
    for (int i = 0; i < kids; ++i)
        if (iris::SceneNode *c = node->childAt(i)) walk(c->sharedFromThis(), fn);
}

/// `path` and every ancestor of it, root-first.
inline QStringList chainOf(const QString &path)
{
    QStringList out;
    if (path.isEmpty()) return out;
    const QStringList parts = path.split(QLatin1Char('/'));
    QString acc;
    for (const QString &p : parts) {
        acc = acc.isEmpty() ? p : acc + QLatin1Char('/') + p;
        out.append(acc);
    }
    return out;
}

}   // namespace detail

using detail::walk;
using detail::chainOf;

inline QString normalize(const QString &path)
{
    QString s = path;
    s.replace(QLatin1Char('\\'), QLatin1Char('/'));
    const QStringList parts = s.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    QStringList clean;
    for (const QString &p : parts) {
        const QString t = p.trimmed();
        if (!t.isEmpty()) clean.append(t);
    }
    return clean.join(QLatin1Char('/'));
}

inline QString parentOf(const QString &path)
{
    const int i = path.lastIndexOf(QLatin1Char('/'));
    return i < 0 ? QString() : path.left(i);
}

inline QString leafOf(const QString &path)
{
    const int i = path.lastIndexOf(QLatin1Char('/'));
    return i < 0 ? path : path.mid(i + 1);
}

inline QStringList all(const iris::ScenePtr &scene)
{
    QStringList out;
    if (!scene) return out;
    QSet<QString> seen;
    const auto add = [&](const QString &raw) {
        const QString p = normalize(raw);
        if (p.isEmpty()) return;
        for (const QString &step : chainOf(p))
            if (!seen.contains(step)) { seen.insert(step); out.append(step); }
    };
    for (const QString &f : scene->folders) add(f);
    walk(scene->getRootNode(), [&](const iris::SceneNodePtr &n) { add(n->folderPath); });
    out.sort(Qt::CaseInsensitive);
    return out;
}

inline bool exists(const iris::ScenePtr &scene, const QString &path)
{
    const QString p = normalize(path);
    if (p.isEmpty()) return true;   // the root level always exists
    return all(scene).contains(p);
}

inline bool create(const iris::ScenePtr &scene, const QString &path)
{
    if (!scene) return false;
    const QString p = normalize(path);
    if (p.isEmpty()) return false;
    if (exists(scene, p)) return false;
    for (const QString &step : chainOf(p))
        if (!scene->folders.contains(step)) scene->folders.append(step);
    return true;
}

inline bool setNodeFolder(const iris::ScenePtr &scene, const iris::SceneNodePtr &node,
                   const QString &path)
{
    if (!node) return false;
    const QString p = normalize(path);
    // A brand-new path becomes a folder by the act of putting something in it —
    // exactly how a "New Folder…" from the context menu is expected to behave.
    if (!p.isEmpty() && scene) {
        for (const QString &step : chainOf(p))
            if (!scene->folders.contains(step)) scene->folders.append(step);
    }
    node->_setFolderPath(p);
    if (scene) normalizeList(scene);
    return true;
}

inline bool rename(const iris::ScenePtr &scene, const QString &path, const QString &newLeaf)
{
    if (!scene) return false;
    const QString from = normalize(path);
    if (from.isEmpty() || !exists(scene, from)) return false;
    const QString leaf = normalize(newLeaf);
    if (leaf.isEmpty() || leaf.contains(QLatin1Char('/'))) return false;
    const QString parent = parentOf(from);
    const QString to = parent.isEmpty() ? leaf : parent + QLatin1Char('/') + leaf;
    if (to == from) return true;
    if (exists(scene, to)) return false;

    const auto repath = [&](const QString &p) -> QString {
        if (p == from) return to;
        if (p.startsWith(from + QLatin1Char('/'))) return to + p.mid(from.size());
        return p;
    };

    QStringList folders;
    for (const QString &f : scene->folders) {
        const QString moved = repath(normalize(f));
        if (!moved.isEmpty() && !folders.contains(moved)) folders.append(moved);
    }
    // The destination must exist even if the source was only implicit.
    for (const QString &step : chainOf(to))
        if (!folders.contains(step)) folders.append(step);
    scene->folders = folders;

    walk(scene->getRootNode(), [&](const iris::SceneNodePtr &n) {
        const QString p = normalize(n->folderPath);
        const QString moved = repath(p);
        if (moved != n->folderPath) n->_setFolderPath(moved);
    });
    normalizeList(scene);
    return true;
}

inline bool remove(const iris::ScenePtr &scene, const QString &path)
{
    if (!scene) return false;
    const QString from = normalize(path);
    if (from.isEmpty() || !exists(scene, from)) return false;
    const QString parent = parentOf(from);

    // Members and SUB-FOLDERS move up one level; nothing is ever deleted.
    const auto lifted = [&](const QString &p) -> QString {
        if (p == from) return parent;
        if (p.startsWith(from + QLatin1Char('/'))) {
            const QString tail = p.mid(from.size() + 1);
            return parent.isEmpty() ? tail : parent + QLatin1Char('/') + tail;
        }
        return p;
    };

    QStringList folders;
    for (const QString &f : scene->folders) {
        const QString p = normalize(f);
        if (p == from) continue;            // the folder itself is gone
        const QString moved = lifted(p);
        if (!moved.isEmpty() && !folders.contains(moved)) folders.append(moved);
    }
    scene->folders = folders;

    walk(scene->getRootNode(), [&](const iris::SceneNodePtr &n) {
        const QString p = normalize(n->folderPath);
        if (p.isEmpty()) return;
        const QString moved = lifted(p);
        if (moved != n->folderPath) n->_setFolderPath(moved);
    });
    normalizeList(scene);
    return true;
}

inline void normalizeList(const iris::ScenePtr &scene)
{
    if (!scene) return;
    QStringList clean;
    for (const QString &f : scene->folders) {
        const QString p = normalize(f);
        if (p.isEmpty()) continue;
        for (const QString &step : chainOf(p))
            if (!clean.contains(step)) clean.append(step);
    }
    clean.sort(Qt::CaseInsensitive);
    scene->folders = clean;
}

inline QList<iris::SceneNodePtr> membersOf(const iris::ScenePtr &scene, const QString &path)
{
    QList<iris::SceneNodePtr> out;
    if (!scene) return out;
    const QString p = normalize(path);
    walk(scene->getRootNode(), [&](const iris::SceneNodePtr &n) {
        if (n == scene->getRootNode()) return;
        if (normalize(n->folderPath) == p) out.append(n);
    });
    return out;
}

inline Snapshot snapshot(const iris::ScenePtr &scene)
{
    Snapshot snap;
    if (!scene) return snap;
    snap.folders = scene->folders;
    walk(scene->getRootNode(), [&](const iris::SceneNodePtr &n) {
        if (!n->folderPath.isEmpty()) snap.membership.insert(n->getGUID(), n->folderPath);
    });
    return snap;
}

inline void restore(const iris::ScenePtr &scene, const Snapshot &snap)
{
    if (!scene) return;
    scene->folders = snap.folders;
    walk(scene->getRootNode(), [&](const iris::SceneNodePtr &n) {
        const QString want = snap.membership.value(n->getGUID());
        if (n->folderPath != want) n->_setFolderPath(want);
    });
}

inline void writeEditorBlock(QJsonObject &editorObj, const iris::ScenePtr &scene)
{
    if (!scene) return;
    QJsonArray list;
    for (const QString &f : scene->folders) list.append(f);
    editorObj["folders"] = list;

    QJsonObject membership;
    walk(scene->getRootNode(), [&](const iris::SceneNodePtr &n) {
        if (!n->folderPath.isEmpty()) membership.insert(n->getGUID(), n->folderPath);
    });
    editorObj["nodeFolders"] = membership;
}

inline void readEditorBlock(const QJsonObject &editorObj, const iris::ScenePtr &scene)
{
    if (!scene) return;
    scene->folders.clear();
    const QJsonArray list = editorObj.value(QStringLiteral("folders")).toArray();
    for (const QJsonValue &v : list) {
        const QString p = normalize(v.toString());
        if (!p.isEmpty() && !scene->folders.contains(p)) scene->folders.append(p);
    }
    const QJsonObject membership = editorObj.value(QStringLiteral("nodeFolders")).toObject();
    if (!membership.isEmpty()) {
        walk(scene->getRootNode(), [&](const iris::SceneNodePtr &n) {
            const QString p = normalize(membership.value(n->getGUID()).toString());
            if (!p.isEmpty()) n->_setFolderPath(p);
        });
    }
    normalizeList(scene);
}

}   // namespace scenefolders

#endif   // SCENEFOLDERS_H
