/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENEPROPERTYCOMMAND_H
#define SCENEPROPERTYCOMMAND_H

// ScenePropertyCommand — one WORLD-panel row edit, undoable (debt L6 / N5).
//
// The node panels have had SetNodePropertyCommand (a key + two QVariants
// replayed through the node's own reflection) since the scripting audit, and
// the quality rows have WorldModeCommand (a registry snapshot, because a tier
// writes thirteen fields at once). Between them sat every remaining World-panel
// row — fog, ambient, gravity, play mode, the GI volume, the post-process
// parameters, the looks stack, the whole sky block — which wrote
// `scene->field = value` from the slot and left NOTHING on the undo stack.
// That was the API-first INVERSION the debt re-audit recorded: the scripting
// side promises "one run is one undo step" while the panel the user actually
// drives promised nothing.
//
// This is the scene-side twin of SetNodePropertyCommand and it is deliberately
// the same shape: a KEY and two values, replayed through one table
// (sceneprops::) that says how a world property is read and written. The table
// is the single answer to "what does a World panel row write", so a test can
// drive every row by name and a future verb can reuse the same accessor rather
// than inventing a second spelling of `scene->fogDensity`.
//
// What it is NOT: it does not carry the World Mode pin map. A row that PINS
// (msaa, shadow resolution, the Rayon rows, sky detail, ambient-from-sky, the
// post-process on/off rows) is a registry row and belongs to WorldModeCommand,
// which restores the backing field AND the pin — using this class there would
// undo the value and leave the pin behind.

#include <functional>

#include <QString>
#include <QStringList>
#include <QVariant>

#include "commands/studiocommand.h"
#include "irisgl/irisglfwd.h"

/// The world properties a panel row can write, by name.
///
/// Keys are the document's own field names (`fogDensity`, `ambientColor`,
/// `giBoundsMin`, ...) with two composites that only make sense whole:
///   * `postFx.<id>`  — one worldmodes::postFxParam (exposure, bloomKnee, ...);
///                      the registry owns the clamp and the setter.
///   * `sky`          — the sky block (type, per-type blobs, the live colour /
///                      gradient / analytic fields). One value, because a sky
///                      edit writes the blob AND the live field, and undoing
///                      half of that leaves the panel showing a sky the
///                      renderer is not drawing.
namespace sceneprops {

using Getter = std::function<QVariant(const iris::ScenePtr &)>;
using Setter = std::function<void(const iris::ScenePtr &, const QVariant &)>;

struct Field
{
    QString id;
    Getter get;
    Setter set;
};

/// Every known key, in declaration order. Built once.
const QVector<Field> &fields();
const Field *field(const QString &id);
QStringList ids();

/// Reads / writes a world property by key. `set` answers false for an unknown
/// key (and writes nothing) rather than half-applying it.
QVariant get(const iris::ScenePtr &scene, const QString &id);
bool set(const iris::ScenePtr &scene, const QString &id, const QVariant &value);

}   // namespace sceneprops

class ScenePropertyCommand : public StudioCommand
{
public:
    /// `before` must be read BEFORE the edit; the panel applies the edit
    /// itself (live, so the viewport follows a drag) and pushes this after.
    ScenePropertyCommand(const QString &text, const iris::ScenePtr &scene, const QString &key,
                         const QVariant &before, const QVariant &after);

    void undo() override;
    void redo() override;

    /// Called after every undo/redo so the panel that owns the row re-reads it.
    /// Optional (null in scripts and tests).
    void setRefresh(std::function<void()> refresh) { mRefresh = std::move(refresh); }

    /// The key this command carries (for tests and for merge decisions).
    QString key() const { return mKey; }

private:
    void apply(const QVariant &value);

    iris::SceneWPtr mScene;
    QString mKey;
    QVariant mBefore;
    QVariant mAfter;
    std::function<void()> mRefresh;
    /// QUndoStack::push() replays redo() immediately on an edit the panel has
    /// already applied — the same guard WorldModeCommand carries, and here it
    /// also keeps the refresh from rebuilding the row whose signal is still on
    /// the stack.
    bool mFirstRedo = true;
};

#endif   // SCENEPROPERTYCOMMAND_H
