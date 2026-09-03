/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef WORLDMODES_H
#define WORLDMODES_H

// World Modes — the per-scene scalability system (POST_CHAIN_SPEC.md §9).
//
// ONE TABLE, ONE FILE, in the ShortcutRegistry mould: every quality row the
// user can see declares itself here — id, label, group, value type, the four
// tier values, the backing field it writes, and a one-line "what this costs"
// string. The World panel section and the world.* verbs are GENERATED from this
// table; neither of them knows any row by name. A row the engine cannot serve
// yet declares itself `available = false` and renders disabled — which is how
// the planar-reflection lane plugs its budget in later without touching a
// single consumer of this file.
//
// STORAGE IS WRITE-THROUGH. The per-scene fields that already existed
// (antiAliasing, shadowResolution, shadowFilterTier, giMode, giQuality,
// skyBakeResolution, ambientFromSky) stay the backing store and the single
// source of truth for SceneMirror, the serializer and every existing panel.
// setMode() writes the tier value into each backing field EXCEPT rows the user
// pinned in Scene::worldOverrides. The invariant is one line and one test:
//
//     a backing field is ALWAYS the resolved value.
//
// Editing a row through setRowValue() writes the field AND records the
// override; clearOverride() drops the override and writes the tier value back.
// Panels that edit a backing field directly (World > Anti-Aliasing, World >
// Shadows) must route through setRowValue() or the override bookkeeping goes
// stale — that is the bug this design invites, and the one the tests pin.

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

#include "irisgl/irisglfwd.h"

namespace worldmodes {

/// Tier. Custom means "no tier applied": the backing fields are whatever the
/// document/user set, and every row reports source "custom".
enum class Mode { Custom = -1, Low = 0, Medium = 1, High = 2, Epic = 3 };

/// How a row's integer value is presented.
enum class RowType { Bool, Enum, Int };

/// One choice of an Enum row. `value` is what lands in the backing field.
struct EnumOption {
    QString id;      ///< stable, script-facing ("vct", "4x", "auto")
    QString label;   ///< human ("VCT", "4x", "Auto")
    int     value;   ///< the backing-field value
};

struct Row {
    QString  id;                       ///< stable, script-facing
    QString  label;
    QString  group;                    ///< panel grouping ("Rendering", "Shadows", ...)
    RowType  type = RowType::Int;
    QVector<EnumOption> options;       ///< Enum rows only
    int      minValue = 0, maxValue = 0;   ///< Int rows only
    int      tier[4] = { 0, 0, 0, 0 };  ///< Low, Medium, High, Epic
    QString  cost;                     ///< one line, shown as the row tooltip
    bool     available = true;         ///< false = declared but not yet implemented

    /// The backing field. Both are null for a row with no backing field yet
    /// (`available == false`): its value lives only in worldOverrides.
    std::function<int(const iris::ScenePtr &)>       get;
    std::function<void(const iris::ScenePtr &, int)> set;
};

/// The registry. Built once, never mutated.
const QVector<Row> &rows();
/// The row with this id, or null.
const Row *row(const QString &id);

QString    modeName(Mode m);            ///< "custom" | "low" | "medium" | "high" | "epic"
Mode       modeFromName(const QString &name, bool *ok = nullptr);
QStringList modeNames();                ///< low, medium, high, epic (Custom is not pickable)

/// The scene's tier.
Mode mode(const iris::ScenePtr &scene);
/// Applies a tier: writes each row's tier value into its backing field, EXCEPT
/// rows present in scene->worldOverrides (overrides survive mode switches).
void setMode(const iris::ScenePtr &scene, Mode m);

/// The row's tier value for `m`, or the row's current value when m == Custom.
int tierValue(const Row &r, Mode m, const iris::ScenePtr &scene);
/// The RESOLVED value — by the write-through invariant this is simply the
/// backing field (or the recorded override for a row with no backing field).
int resolved(const iris::ScenePtr &scene, const Row &r);

/// Where the resolved value came from: "override", "mode" or "custom".
QString source(const iris::ScenePtr &scene, const Row &r);

/// Writes a row's value. `recordOverride` false is the internal path used by
/// setMode()/clearOverride(); every user- or script-driven edit records.
/// Values are validated against the row (enum membership / int range); an
/// invalid value is refused and returns false.
bool setRowValue(const iris::ScenePtr &scene, const QString &id, int value,
                 bool recordOverride = true);
/// Convenience for Enum rows: sets by option id ("vct", "4x", "auto").
bool setRowValueByOptionId(const iris::ScenePtr &scene, const QString &id,
                           const QString &optionId);

/// Records a pin for a value the caller has ALREADY written to the backing
/// field. This is the path the pre-existing setters take (world.setAntiAliasing,
/// world.setShadowResolution, the World > Anti-Aliasing and World > Shadows
/// panels): they validate in their own, wider terms — setShadowResolution
/// accepts any value in 256..8192, not just the five the row lists — but the
/// override bookkeeping must still see the edit or a later mode switch would
/// silently undo it. Unknown ids are ignored.
void pinRowValue(const iris::ScenePtr &scene, const QString &id, int value);

/// Drops the pin and writes the tier value back (no-op in Custom mode beyond
/// dropping the pin, since there is no tier to fall back to).
bool clearOverride(const iris::ScenePtr &scene, const QString &id);
void clearOverrides(const iris::ScenePtr &scene);

/// The label of the option holding `value`, or the number as text.
QString valueLabel(const Row &r, int value);
/// The option id holding `value`, or the number as text — the script-facing
/// spelling of a row's value.
QString valueId(const Row &r, int value);
/// The value an option id (or a plain number) means; false when unparseable.
bool valueFromId(const Row &r, const QString &id, int &out);

}   // namespace worldmodes

#endif   // WORLDMODES_H
