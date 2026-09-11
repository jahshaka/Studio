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
    /// TIER SPACE (GI_UNIFIED_SPEC.md §2 — the Rayon unification). A row is
    /// resolved by the WORLD mode by default; a `rayonTiered` row is resolved by
    /// the scene's RAYON tier instead, and its `tier[]` columns are the Rayon
    /// tiers (Low/Medium/High/Epic of the GI dial), not the world's.
    ///
    /// It exists because two dials must never own one backing field. Rayon's
    /// technique, quality and DDGI rows used to be world-mode rows; the world
    /// mode now drives the single `rayon` row, and THAT row writes the five
    /// Rayon rows (technique, quality, field, bounces, dynamic probes) through.
    /// setMode() therefore skips them (the rayon row already wrote them,
    /// honouring their pins) and tierValue() reads their Rayon column.
    bool     rayonTiered = false;

    /// The backing field. Both are null for a row with no backing field yet
    /// (`available == false`): its value lives only in worldOverrides.
    std::function<int(const iris::ScenePtr &)>       get;
    std::function<void(const iris::ScenePtr &, int)> set;
};

/// A CONTINUOUS post-process parameter — the other half of a post-fx row.
///
/// The tier table answers "how much machinery" (HDR on? which SMAA preset?);
/// these answer "how does it LOOK", and they are deliberately NOT tiered: a
/// mode switch must never silently regrade somebody's scene (world.postFx's own
/// note). But they were nowhere declared, so the World panel had no way to
/// offer them and the verb's ranges lived only inside the verb — the pattern
/// this whole file exists to end.
///
/// ONE TABLE, TWO CONSUMERS, exactly like Row: the World > Post Process section
/// builds a scrubbable row per entry and world.postFx reads and writes through
/// the same entries, so a range can never mean two different things.
struct ParamRow {
    QString id;          ///< script-facing, and the world.postFx key ("exposure")
    QString label;       ///< human ("Exposure")
    QString ownerRowId;  ///< the Row this belongs under ("hdr", "bloom", "ssao")
    double  minValue = 0.0, maxValue = 1.0;
    double  perPixelStep = 0.02;   ///< scrub sensitivity in the panel
    int     decimals = 2;
    QString doc;         ///< the row tooltip AND the verb's documentation

    std::function<double(const iris::ScenePtr &)>       get;
    std::function<void(const iris::ScenePtr &, double)> set;
};

/// The continuous post-process parameters, in panel order.
const QVector<ParamRow> &postFxParams();
/// The parameter with this id, or null.
const ParamRow *postFxParam(const QString &id);
/// The ids of the Row entries the World > Post Process section shows, in order.
/// (The post chain's rows, as opposed to shadows/GI/reflections, which have
/// sections of their own.)
const QStringList &postFxRowIds();

/// The registry. Built once, never mutated.
const QVector<Row> &rows();
/// The row with this id, or null.
const Row *row(const QString &id);

// ---------------------------------------------------------------------------
// RAYON — the unified realtime-GI switch (GI_UNIFIED_SPEC.md §2 / P2).
//
// ONE dial where there were five: the World panel shows an on/off toggle, a
// quality tier and the update budget, and everything the tier consumes moves
// under an Advanced disclosure. NOTHING new was invented to do it — a Rayon
// tier is a registry row (`rayon`) whose write-through targets are five other
// registry rows (`giMode`, `giQuality`, `giDdgi`, `giBounces`,
// `giDynamicProbes`, all `rayonTiered`). The invariant is the same one line as
// everywhere else in this file:
//
//     a backing field is ALWAYS the resolved value.
//
// THE TABLE (spec §2; owner decision 2026-09-09 night, option (b): Medium and
// High are DDGI-fed — the field is the only diffuse arm that is right in both
// open and sealed scenes (rayon2 S1-S3) — and Epic has a column of its own so
// it no longer collapses onto High). ONE table, ONE owner: `kRayonTable` in
// worldmodes.cpp; the engine's GiQuality stays three-valued (it is the
// RESOLUTION dial — voxels, probe faces — and Epic changes no resolution), so
// Epic's two extra columns are ordinary document fields the engine already
// reads (numBounces) or now reads (dynamicProbes), written through like the
// other three.
//
//   tier    technique             voxels  ddgi  ddgiSource  DDGI grid  bounces  dynamicProbes  probe faces/HDR/shadows  budget
//   Low     Instant Radiosity     —       off   —           —          1        0              —                        (dial)
//   Medium  VCT                   64^3    ON    auto=voxel  8192 fit   1        0              — (no probes)            (dial)
//   High    VCT + probes (hybrid) 128^3   ON    auto=voxel  8192 fit   1        0              512 / HDR / shadowed     (dial)
//   Epic    VCT + probes (hybrid) 128^3   ON    auto=voxel  8192 fit   3        0 (was 2)      512 / HDR / shadowed     (dial)
//
// Derived columns (not rows): voxels and probe faces/HDR/shadows follow
// `giQuality` (OgreGi.cpp giVoxelResolution / buildPcc); the DDGI grid is the
// engine's fixed 8192-probe aspect fit (kIfdTotalProbes); ddgiSource "auto" is
// voxel at every tier — the raster feed (3.4-9 ms per probe, rayon2 S3) is an
// Advanced opt-in and never a default. Epic's bounce column is measured:
// bounces 1 -> 3 raises the DDGI-fed floor bounce (gi.ddgi case 7). Epic's
// dynamic probes went 2 -> 0 on 2026-09-12 (REALTIME_REFLECTIONS_SPEC R0): the
// alive-scene baseline measured the two per-frame mover captures at ~40 ms
// (87 -> 47 ms/frame with things moving), and movers are reflected every frame
// by SSR + planar instead. The engine feature stays reachable as a pinned
// setting until lane R2 deletes the column (gi.dynamic_probes still covers it).
//
// The GI UPDATE BUDGET is deliberately NOT in the table: it is a "how fast may
// this keep up" control, not a "how much machinery" one, and it stays a visible
// row of its own (owner decision D5); a pinned dynamicProbes setting rides ON TOP of it.
//
// WHETHER RAYON IS ON is `scene->giMode != OFF` — there is no second flag.
// `scene->giTier` remembers the quality across an off/on trip.
enum class RayonTier { Low = 0, Medium = 1, High = 2, Epic = 3 };

/// The registry id of the tier row, and of the five rows it writes through
/// (giMode, giQuality, giDdgi, giBounces, giDynamicProbes — in that order).
QString     rayonRowId();
QStringList rayonRowIds();

QString     rayonTierName(RayonTier t);        ///< "low" | "medium" | "high" | "epic"
RayonTier   rayonTierFromName(const QString &name, bool *ok = nullptr);
QStringList rayonTierNames();

/// The scene's Rayon tier (what quality it comes back at), whether GI is on or
/// off; and whether GI is on at all.
RayonTier rayonTier(const iris::ScenePtr &scene);
bool      rayonEnabled(const iris::ScenePtr &scene);

/// What the tier resolves each of its rows to — THE TABLE, read one column at
/// a time. `technique` is a GiMode ordinal, `quality` a GiQuality ordinal,
/// `ddgi` 0/1, `bounces` the total light bounces (1..4), `dynamicProbes` the
/// per-frame moved-covering probe re-captures reserved on top of the budget.
int rayonTechnique(RayonTier t);
int rayonQuality(RayonTier t);
int rayonDdgi(RayonTier t);
int rayonBounces(RayonTier t);
int rayonDynamicProbes(RayonTier t);

/// Applies a Rayon state: records the tier, writes each `rayonTiered` row's
/// tier value into its backing field EXCEPT rows the user pinned, and writes
/// giMode (OFF when disabled, the resolved technique when enabled).
///
/// Turning Rayon OFF drops a pinned TECHNIQUE (`giMode`): the pin and the
/// enable share one field, and remembering "off, but pinned to VCT" would be a
/// state nothing can render. Everything else — a pinned quality, a pinned DDGI
/// — survives the trip, which is what makes the toggle non-destructive.
void setRayon(const iris::ScenePtr &scene, bool enabled, RayonTier tier);

/// True when a `rayonTiered` row RESOLVES to something other than the tier's
/// value — the honest "Custom" indicator for the tier row. False whenever Rayon
/// is off (there is nothing to deviate from: the picture is no GI either way).
bool rayonCustom(const iris::ScenePtr &scene);
/// The labels of the deviating rows, for the tier row's tooltip.
QStringList rayonDeviations(const iris::ScenePtr &scene);
/// Drops the pins on the `rayonTiered` rows and re-applies the tier.
void clearRayonOverrides(const iris::ScenePtr &scene);

/// MIGRATION (spec §2's table), for a document written before the tier existed:
/// derives the tier its serialized GI settings correspond to, PINS every field
/// that deviates from that tier, and pins the tier row itself when the scene's
/// World Mode would resolve it to something else. Technique, quality, bounces
/// and dynamic probes are preserved exactly (a deviation becomes a pin), so
/// those render IDENTICALLY by construction. The ONE field that may move is
/// the irradiance field: a document's tri-state -1 ("auto") means "the tier
/// decides", and since option (b) Medium and High decide ON — that is the
/// owner's re-pin of the shipped vct+medium samples, taken here and nowhere
/// else; an explicit 0/1 in the document is preserved (pinned if it deviates).
/// Redundant pins (a pinned value that IS the derived tier's) are dropped, so a
/// migrated scene reads as its tier and not as "Custom". Epic is never
/// derived: no pre-tier document could have rendered its columns.
void deriveRayonFromDocument(const iris::ScenePtr &scene);

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
