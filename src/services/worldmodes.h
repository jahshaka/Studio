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
// (antiAliasing, shadowResolution, shadowFilterTier, giMode, giQuality)
// stay the backing store and the single
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

namespace jahshaka { namespace engine { struct GiGatherFacts; } }

namespace worldmodes {

/// Tier. Custom means "no tier applied": the backing fields are whatever the
/// document/user set, and every row reports source "custom".
enum class Mode { Custom = -1, Low = 0, Medium = 1, High = 2, Epic = 3 };

/// How a row's integer value is presented.
enum class RowType { Bool, Enum, Int };

/// Which dial resolves a row (Row::tierSpace). The names are the ones
/// `world.modeTable` reports.
enum class TierSpace { World, Photon, None };

/// One choice of an Enum row. `value` is what lands in the backing field.
struct EnumOption {
    QString id;      ///< stable, script-facing ("vct", "4x", "auto")
    QString label;   ///< human ("VCT", "4x", "Auto")
    int     value = 0;   ///< the backing-field value
};

struct Row {
    QString  id;                       ///< stable, script-facing
    QString  label;
    QString  group;                    ///< panel grouping ("Rendering", "Shadows", ...)
    RowType  type = RowType::Int;
    QVector<EnumOption> options;       ///< Enum rows only
    int      minValue = 0, maxValue = 0;   ///< Int rows only
    int      tier[4] = { 0, 0, 0, 0 };  ///< Low, Medium, High, Epic
    /// The row's tooltip text when it is the same on every machine. READ IT
    /// THROUGH rowCost(), never directly: a row whose text depends on whether
    /// this machine's rays resolve carries `costAt` instead and leaves this empty.
    QString  cost;
    /// THE TEXT OF A ROW WHOSE TRUTH DEPENDS ON THE MACHINE (STUDIO-CRUD-1
    /// item 6). After PHOTON-F12-PCC no shipped tier builds a reflection-probe
    /// grid wherever the scene traces rays, so a row that explains reflections
    /// at High and Epic says one thing on a tracing machine and another off it.
    /// The ONE text is computed from `sceneTracesRays` (through
    /// tierRaysResolve), never two hand-kept literals.
    std::function<QString(bool sceneTracesRays)> costAt;
    /// An Enum row whose option NAMES depend on the SCENE on this machine (the
    /// technique: ordinal 2 is "VCT + rays" where probeGridByRays(scene,
    /// sceneTracesRays) holds, "VCT + probes" where it does not — the ONE rule
    /// the GI section and world.gi read too). Null = the options' own labels.
    /// Read through optionLabel().
    std::function<QString(int value, const iris::ScenePtr &scene, bool sceneTracesRays)>
        optionLabelAt;
    bool     available = true;         ///< false = declared but not yet implemented
    /// TIER SPACE — WHICH DIAL, IF ANY, RESOLVES THIS ROW.
    ///
    ///  * `World`  (the default) — the World Mode tier; `tier[]` is its four
    ///    columns.
    ///  * `Photon` (GI_UNIFIED_SPEC.md §2 — the Photon unification) — the
    ///    scene's PHOTON tier, and `tier[]` is the Photon columns, not the
    ///    world's. It exists because two dials must never own one backing
    ///    field: Photon's technique, quality and DDGI rows used to be
    ///    world-mode rows; the world mode now drives the single `photon` row,
    ///    and THAT row writes them through. setMode() skips them and
    ///    tierValue() reads their Photon column.
    ///  * `None`   (EXPOSURE-1) — NO tier resolves it. `tier[]` is not read,
    ///    setMode() never writes it, and its value is simply the backing field
    ///    the user (or the document) put there. It is for settings that are not
    ///    a scalability question at all — how a scene is EXPOSED is an art
    ///    decision, and a mode switch must never regrade somebody's picture
    ///    (the same reasoning the continuous ParamRows carry). Such a row still
    ///    PINS when it is edited, so `source()` reads "override" and the panel
    ///    marks it, and a mode switch leaves it alone whether it is pinned or
    ///    not.
    TierSpace tierSpace = TierSpace::World;

    /// WHEN THIS ROW EXISTS AT ALL. Null (the common case) = always. Reserved,
    /// exactly as ParamRow::visible is, for a row that would be a LIE in the
    /// current mode rather than merely inert: the METERING PATTERN describes
    /// how a measurement is taken, and Manual exposure takes none (EXPOSURE-2).
    /// A dead row is greyed, not hidden — "why is it grey" beats "where did it
    /// go" — so this is for the one class where the row is not a control.
    std::function<bool(const iris::ScenePtr &)> visible;

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

    /// WHEN THIS PARAMETER IS LIVE. Null (the common case) means "its owner row
    /// is on", which is the right rule for a Bool owner and the only rule this
    /// table had. A parameter under an ENUM owner needs to say so itself.
    std::function<bool(const iris::ScenePtr &)> enabled;
    /// WHEN THIS PARAMETER EXISTS AT ALL. Null = always. A parameter that is
    /// dead weight is greyed rather than hidden ("why is it grey" beats "where
    /// did it go"), so this is reserved for one that would be a LIE: the
    /// auto-exposure window under Manual exposure bounds a measurement that is
    /// not being made (EXPOSURE-1). The camera's panel has hidden its own pair
    /// the same way since CAMERA_LENS_SPEC §4.
    std::function<bool(const iris::ScenePtr &)> visible;

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

/// A row's tooltip text on a machine where the scene does (`sceneTracesRays`)
/// or does not trace rays — IEditorViewport::sceneTracesRays, false with no
/// engine. EVERY surface that shows a row's text reads it here (the World
/// panels, world.modeTable), so a text cannot be right on one and stale on
/// another.
QString rowCost(const Row &r, bool sceneTracesRays);
/// An Enum row option's display name for `scene` on this machine (see
/// Row::optionLabelAt). Every surface that names an option reads it here.
QString optionLabel(const Row &r, const EnumOption &o, const iris::ScenePtr &scene,
                    bool sceneTracesRays);

// ---------------------------------------------------------------------------
// PHOTON — the unified realtime-GI switch (GI_UNIFIED_SPEC.md §2 / P2).
//
// ONE dial where there were five: the World panel shows an on/off toggle, a
// quality tier and the update budget, and everything the tier consumes moves
// under an Advanced disclosure. NOTHING new was invented to do it — a Photon
// tier is a registry row (`photon`) whose write-through targets are four other
// registry rows (`giMode`, `giQuality`, `giDdgi`, `giBounces`, all
// Photon-tiered). The invariant is the same one line as
// everywhere else in this file:
//
//     a backing field is ALWAYS the resolved value.
//
// THE TABLE (spec §2; owner decision 2026-09-09 night, option (b): Medium and
// High are DDGI-fed — the field is the only diffuse arm that is right in both
// open and sealed scenes (rayon2 S1-S3) — and Epic has a column of its own so
// it no longer collapses onto High). ONE table, ONE owner: `kPhotonTable` in
// worldmodes.cpp; the engine's GiQuality stays three-valued (it is the
// RESOLUTION dial — voxels, probe faces — and Epic changes no resolution), so
// Epic's two extra columns are ordinary document fields the engine already
// reads (numBounces), written through like the other two.
//
//   tier    technique             voxels             ddgi  DDGI grid  bounces  probe faces/HDR/shadows  budget
//   Low     VCT, 2 cascades       64^3               ON    8192 fit   1        — (no probes)            (dial)
//   Medium  VCT, 4 cascades       64^3               ON    8192 fit   1        — (no probes)            (dial)
//   High    the hybrid            128^3 near, 64^3   ON    8192 fit   1        512 / HDR / shadowed *   (dial)
//   Epic    the hybrid            128^3 near, 64^3   ON    8192 fit   3        512 / HDR / shadowed *   (dial)
//
// * The hybrid is "VCT + rays" wherever the scene traces rays on this machine
//   (High's rayReflections: the rays are the reflection and NO probe grid is
//   built, PHOTON-F12-PCC) and "VCT + probes" where it does not
//   (techniqueLabel); the probe columns apply only in the second case.
//
// Derived columns (not rows): voxels and probe faces/HDR/shadows follow
// `giQuality` (OgreGi.cpp giVoxelResolution / buildPcc); the DDGI grid is the
// engine's fixed 8192-probe aspect fit (kIfdTotalProbes). Epic's bounce column
// is measured:
// bounces 1 -> 3 raises the DDGI-fed floor bounce (gi.ddgi case 7).
//
// THE FIFTH COLUMN IS GONE (lane R2, 2026-09-12). "Dynamic probes" reserved
// extra probe re-captures per frame for the probes a MOVING object was inside;
// R0 set it to 0 in every tier after the alive-scene baseline measured it, and
// R2 deleted the feature outright, because a moving object is no longer in a
// probe capture at all (it is reflected every frame by SSR and the planar
// mirrors instead). Scenes saved with the old key simply ignore it.
//
// The GI UPDATE BUDGET is deliberately NOT in the table: it is a "how fast may
// this keep up" control, not a "how much machinery" one, and it stays a visible
// row of its own (owner decision D5).
//
// WHETHER PHOTON IS ON is `scene->giMode != OFF` — there is no second flag.
// `scene->giTier` remembers the quality across an off/on trip.
enum class PhotonTier { Low = 0, Medium = 1, High = 2, Epic = 3 };

/// The registry id of the tier row, and of the four rows it writes through
/// (giMode, giQuality, giDdgi, giBounces — in that order).
QString     photonRowId();
QStringList photonRowIds();

QString     photonTierName(PhotonTier t);        ///< "low" | "medium" | "high" | "epic"
PhotonTier   photonTierFromName(const QString &name, bool *ok = nullptr);
QStringList photonTierNames();

/// The scene's Photon tier (what quality it comes back at), whether GI is on or
/// off; and whether GI is on at all.
PhotonTier photonTier(const iris::ScenePtr &scene);
bool      photonEnabled(const iris::ScenePtr &scene);

/// What the tier resolves each of its rows to — THE TABLE, read one column at
/// a time. `technique` is a GiMode ordinal, `quality` a GiQuality ordinal,
/// `ddgi` 0/1, `bounces` the total light bounces (1..4).
int photonTechnique(PhotonTier t);
int photonQuality(PhotonTier t);
int photonDdgi(PhotonTier t);
int photonBounces(PhotonTier t);
/// The probe capture size column (pixels per cube face; 0 = follow the engine's
/// quality dial). Every tier is 0 today — the column exists so a scene can pin
/// one, which is the owner's 2026-09-13 Q4 decision.
int photonProbeSize(PhotonTier t);
/// The CASCADE CHAIN column (0/1) — Photon's camera-centred voxel cascades.
/// On in every tier since PHOTON_SPEC §7 E2 (6): the bounce follows the camera
/// unless a scene pins `giCascades` off.
int photonCascades(PhotonTier t);
/// THE GATHER COLUMN (PHOTON-GATHER-1d): the screen-probe gather at this tier —
/// on/off, the probe stride, the octahedral resolution, the adaptive cap — as a
/// PROJECTION of the engine's tier table (`giQualityFacts(...).gather`, the
/// quality column and the Epic tier), never a copy: High and Epic on (8 px a
/// probe at Epic), Medium on at 36 rays, Low off. What it resolves to still
/// needs a machine that traces; the document row `giGather` = auto follows it.
jahshaka::engine::GiGatherFacts photonGather(PhotonTier t);
/// ...and its VR COLUMN (PHOTON-GA-VR): the gather a HEADSET gets at this tier —
/// the same projection through `GiViewProfile::Vr`.
jahshaka::engine::GiGatherFacts photonVrGather(PhotonTier t);

/// WHAT A TIER IS, GENERATED FROM THE TABLES — the cure for the five tier
/// tooltips that described a renderer which did not exist (render audit A5,
/// lane CRUD-RENDER-1).
///
/// The numbers come from TWO tables and nowhere else: this file's kPhotonTable
/// (which technique, which quality, how many bounces, the field, the chain) and
/// the ENGINE's own `jahshaka::engine::giQualityFacts` (what a quality dial
/// physically is: the cascade chain and its cells, the single volume's
/// resolution, the probe face size, the HDR/shadow defaults). Nothing here is
/// prose about the renderer that a human has to keep in step.
///
/// `photonTierSentence` is one tier in one sentence ("Low: two camera cascades,
/// 5 m at 64 cubed (0.16 m cells) ... the irradiance field on, 1 bounce, no
/// reflection probes."); `photonTierSummary` is all four, for a tooltip that
/// describes the dial rather than a setting.
QString photonTierSentence(PhotonTier t);
QString photonTierSummary();
/// The VOXEL RESOLUTIONS a tier actually uses, as a phrase: the chain's
/// distinct resolutions when the chain is on ("64", "64 and 128"), which is
/// what the quality dial buys.
QString photonTierVoxelPhrase(PhotonTier t);
/// The probe cube-face size a tier resolves to, in pixels (the engine's own
/// quality dial; 0 is never returned).
int photonTierProbeFaceSize(PhotonTier t);

/// AT A RAY TIER THE PROBE GRID IS NOT BUILT (PHOTON-F12-PCC) — the engine's
/// rule (`OgreScene::probeGridByRays`) read from the DOCUMENT: the scene's
/// quality column says its reflections are traced (the engine's
/// `giQualityFacts(...).rayReflections`: High, which Epic reads) AND
/// `sceneTracesRays` — the scene's Ray Tracing row met with this machine
/// (IEditorViewport::sceneTracesRays; false with no engine). Technique-free on
/// purpose: it answers "would a grid be built here", which is what world.gi's
/// probe keys and the World panel's probe rows ask. False with no scene.
bool probeGridByRays(const iris::ScenePtr &scene, bool sceneTracesRays);
/// The same rule — the same function underneath — for a TIER rather than a
/// scene: the tier's quality column traces its reflections and the scene
/// traces rays on this machine. What the rows' tier-describing TEXTS read; a
/// NAME for the scene in front of the user reads probeGridByRays.
bool tierRaysResolve(PhotonTier t, bool sceneTracesRays);
/// THE TECHNIQUE'S NAME — one source for the World rows, the GI panel and the
/// docs: 0 "Off", 1 "VCT", 2 "VCT + rays" where `raysResolve`, else
/// "VCT + probes".
QString techniqueLabel(int technique, bool raysResolve);
/// The one sentence both surfaces give for it (the verb's refusal, the rows'
/// tooltip).
QString probeGridByRaysReason();

/// Applies a Photon state: records the tier, writes each Photon-tiered row's
/// tier value into its backing field EXCEPT rows the user pinned, and writes
/// giMode (OFF when disabled, the resolved technique when enabled).
///
/// Turning Photon OFF drops a pinned TECHNIQUE (`giMode`): the pin and the
/// enable share one field, and remembering "off, but pinned to VCT" would be a
/// state nothing can render. Everything else — a pinned quality, a pinned DDGI
/// — survives the trip, which is what makes the toggle non-destructive.
void setPhoton(const iris::ScenePtr &scene, bool enabled, PhotonTier tier);

/// True when a Photon-tiered row RESOLVES to something other than the tier's
/// value — the honest "Custom" indicator for the tier row. False whenever Photon
/// is off (there is nothing to deviate from: the picture is no GI either way).
bool photonCustom(const iris::ScenePtr &scene);
/// The labels of the deviating rows, for the tier row's tooltip.
QStringList photonDeviations(const iris::ScenePtr &scene);
/// Drops the pins on the Photon-tiered rows and re-applies the tier.
void clearPhotonOverrides(const iris::ScenePtr &scene);

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
void derivePhotonFromDocument(const iris::ScenePtr &scene);

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

/// Records a pin for a value the caller has ALREADY written to the backing
/// field. This is the path the pre-existing setters take (world.setAntiAliasing,
/// world.setShadowResolution, the World > Anti-Aliasing and World > Shadows
/// panels): they validate in their own, wider terms — setShadowResolution
/// accepts any value in 256..8192, not just the five the row lists — but the
/// override bookkeeping must still see the edit or a later mode switch would
/// silently undo it. Unknown ids are ignored.
void pinRowValue(const iris::ScenePtr &scene, const QString &id, int value);

/// Drops the pin and writes the tier value back (no-op in Custom mode beyond
/// dropping the pin, since there is no tier to fall back to — and likewise for
/// a `TierSpace::None` row, which has no tier at all: its value is the user's
/// and clearing a pin must not reset it).
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
