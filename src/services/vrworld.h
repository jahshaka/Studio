/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef VRWORLD_H
#define VRWORLD_H

// vrworld — THE PROJECT'S VR SETTINGS, and what a session makes of them
// (lane VR-WORLD-1, owner request 2026-09-18: "default fly speed ... and add a
// VR World settings section where we can set that").
//
// TWO THINGS LIVE HERE, and they belong together because they are the same
// subject read from two ends:
//
//   1. THE TABLE (`rows()`) — one declaration per setting: its id (which is
//      the `world.vr` key, the `vr.locomotion` key and the undo key's suffix),
//      its label, its documentation, its range or its named options, how it is
//      read and written ON THE DOCUMENT, and how it is read and written on the
//      resolved `Settings` a session runs with. The World panel's VR section
//      and the two verbs are GENERATED from it, exactly as the World Mode
//      section and `world.postFx` are generated from services/worldmodes.h.
//      Neither consumer names a setting; adding one is a table entry.
//
//      IT IS NOT A worldmodes ROW, deliberately. That registry answers "how
//      much machinery" and resolves through a scalability TIER; how a wearer
//      moves is not a quality question, no tier may ever write it, and two of
//      the values are continuous metres and degrees rather than ints.
//
//   2. THE SESSION (`adopt`/`settings`/`override`) — when a VR session begins
//      the host ADOPTS the project's values as that session's defaults;
//      `vr.locomotion` then overrides any of them for the session, and an
//      override writes NOTHING to the document. So there is exactly one copy
//      of every default (the document's) and the session holds only what
//      somebody overrode, by id.
//
// WHY A FILE-SCOPE HOLDER rather than a service handed around: the three
// consumers (the interaction's per-frame step, the editor preview's fly, the
// Player's fly) sit inside per-frame update maths with no services plumbed
// through, which is the same argument FlySpeedSettings and SnapSettings carry.
// The state is a session's, and there is exactly one session per process
// (Engine::beginVrSession refuses a second).

#include <functional>

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/irisglfwd.h"

namespace vrworld {

/// THE EFFECTIVE VALUES — a project's, with a session's overrides applied.
/// Every member's default IS the document's default (irisgl scene.h), so a
/// Settings built with no scene at all is still the one set of numbers this
/// tree documents.
struct Settings
{
    float flySpeed = iris::kDefaultVrFlySpeed;              ///< metres per second
    iris::VrFlyMode fly = iris::VrFlyMode::Aim;
    iris::VrTurnMode turn = iris::VrTurnMode::Snap;
    float snapTurnDegrees = iris::kDefaultVrSnapTurnDegrees;
    float smoothTurnDegreesPerSecond = iris::kDefaultVrSmoothTurnDegreesPerSecond;
    bool dominantRight = true;
    /// Does a session in this project bind the wearer's BARE HANDS? Off — see
    /// `iris::Scene::vrHands` for why that is a decision and not a placeholder.
    bool hands = false;
};

/// How a row is presented and validated: a continuous NUMBER with a range, a
/// choice among NAMED options, or a FLAG — a true/false the panel shows as a
/// switch and the file carries as a JSON boolean.
///
/// A Flag is not an Enum with two options, and the difference is the one the
/// caller sees: `world.vr({hands:true})` is what anybody writes, and this table
/// refuses a boolean everywhere else (a `true` read as 1 m/s was a real defect,
/// see `validate`). Two options spelled "on"/"off" would make the one honest
/// spelling the wrong one.
enum class RowKind { Number, Enum, Flag };

/// One choice of an Enum row. `value` is what lands in the field.
struct EnumOption
{
    QString id;        ///< stable, script-facing ("aim", "snap", "right")
    QString label;     ///< human ("Aim", "Snap", "Right")
    int     value = 0;     ///< the field's value (an enum ordinal, or 0/1 for a flag)
};

/// One setting. The FOUR accessors are what make the panel and the verbs
/// contain no per-setting code: two read/write the DOCUMENT, two read/write a
/// resolved `Settings` (which is how a session override is applied by id).
///
/// EVERY VALUE IS A DOUBLE, enum ordinals included — one accessor pair instead
/// of two, the same compromise worldmodes::Row makes by carrying ints.
struct Row
{
    QString id;        ///< the verb key, and the undo key's suffix ("vr.<id>")
    /// THE DOCUMENT'S OWN KEY ("vrFlySpeed"), so the writer and the reader are
    /// generated from this table like every other consumer — six keys spelled
    /// by hand in two files was the one place a rename could still go half
    /// done (the Fable read of VR-WORLD-1, item 5).
    QString jsonKey;
    QString label;     ///< the panel row's label
    QString doc;       ///< the row's tooltip AND the verb's documentation
    RowKind kind = RowKind::Number;
    QVector<EnumOption> options;    ///< Enum rows only

    // Number rows only. `minValue` above zero is also what makes "a speed must
    // be positive" one rule in one place.
    double minValue = 0.0, maxValue = 0.0;
    double perPixelStep = 0.1;      ///< scrub sensitivity in the panel
    int    decimals = 1;
    QString unit;                   ///< "m/s", "degrees", "degrees/second"

    /// CAN A SESSION OVERRIDE IT (`vr.locomotion`)? Most rows are read every
    /// frame by the locomotion step, so a session may change its mind about
    /// them; a row the session LATCHES at creation cannot be changed while it
    /// runs, and pretending otherwise would be a verb that silently did
    /// nothing. Such a row is REFUSED by name, with what to call instead.
    bool sessionFixed = false;

    /// When this row is LIVE. Null = always. Greyed, never hidden ("why is it
    /// grey" beats "where did it go"): the snap step means nothing while the
    /// turn is smooth, and the smooth rate means nothing while it snaps.
    std::function<bool(const iris::ScenePtr &)> enabled;

    std::function<double(const iris::ScenePtr &)>       get;
    std::function<void(const iris::ScenePtr &, double)> set;
    std::function<double(const Settings &)>             read;
    std::function<void(Settings &, double)>             write;
};

/// The table. Built once, never mutated.
const QVector<Row> &rows();
/// The row with this id, or null.
const Row *row(const QString &id);
/// Every row id, in panel order — what the verbs list as their known keys.
QStringList ids();

/// The undo / sceneprops key of a row ("vr.flySpeed").
QString propsKey(const QString &rowId);

// ---- THE FILE ------------------------------------------------------------
//
// ONE PAIR OF FUNCTIONS, so a row is saved and read the day it is declared and
// neither half can name a key the other does not. Enum rows ride as their
// stable option id ("aim", "snap", "right") and Number rows as numbers, which
// is the house rule for everything with a name (`rayTracing`, the exposure
// mode, the play mode).

/// Writes every row into `sceneObj` (SceneWriter).
void write(const iris::ScenePtr &scene, QJsonObject &sceneObj);

/// Reads every row the object carries (SceneReader). THE READER-DEFAULTS LAW:
/// an absent key, an unparseable number or a name this build does not know
/// leaves the field exactly as the constructor made it; a number outside the
/// row's range is clamped into it.
void read(const iris::ScenePtr &scene, const QJsonObject &sceneObj);

/// VALIDATES ONE VALUE THE WAY BOTH VERBS AND THE PANEL MUST.
///
/// A Flag takes a true/false and nothing else — a number or a word is refused
/// by name rather than coerced, for the reason the Number rows refuse a
/// boolean: a value of the wrong type is a typo, and a typo that lands is a
/// setting nobody chose.
///
/// A Number must be a finite number inside the row's range: non-numeric,
/// NaN/infinite and non-positive values are REFUSED (`error` says so) and a
/// value merely outside the range is CLAMPED to it, which is what the panel's
/// drag row does and therefore the only behaviour the two can share. An Enum is
/// taken BY NAME, case-insensitively, and an unknown name is refused rather
/// than guessed — a typo that silently meant "aim" would be a session that
/// quietly stopped doing what it was asked.
bool validate(const Row &r, const QVariant &value, double &out, QString &error);

/// The option id holding `value` (Enum rows), or the number as text.
QString valueId(const Row &r, double value);
/// A row's value as the verbs report it: a QString for an Enum, a double for a
/// Number.
QVariant valueOf(const Row &r, const Settings &s);

/// THE DOCUMENT'S OWN VALUES (no session, no overrides). A null scene answers
/// the defaults.
Settings fromScene(const iris::ScenePtr &scene);

// ---- THE SESSION ---------------------------------------------------------

/// THE EFFECTIVE VALUES, and the one call every consumer makes.
///
///   * WHILE A SESSION IS LIVE the defaults are the ones LATCHED when it began
///     (`adopt`), so nothing moves under a wearer mid-flight;
///   * with NO session they are read from `scene` live — which is what a gate
///     injecting controller input at a desktop editor, and a console before the
///     headset is on, must see;
///   * and the session's overrides (`override`) are applied on top in both
///     cases. `scene` may be null.
Settings resolve(const iris::ScenePtr &scene);

/// A SESSION IS BEGINNING: latch this project's values as its defaults. Called
/// by the two hosts that begin one (EditorVrPreview::begin, PlayerVr::begin) —
/// so a project switched between sessions is picked up by the next session.
///
/// IT KEEPS THE OVERRIDES IT FINDS: a `vr.locomotion({...})` issued before the
/// session starts is a caller asking for something, and dropping it at begin
/// was a silent refusal. `release()` is where a session's overrides die.
void adopt(const iris::ScenePtr &scene);

/// A SESSION HAS ENDED: unlatch (reads go back to the live document) and drop
/// the session's overrides. IDEMPOTENT, and called by whichever path notices
/// first — a host's `end()`, or its "the session went away underneath us"
/// branch (a script's `vr.end`, a device loss, the engine tearing a Lost
/// session down all take the second one). A host releases only a session it
/// OWNED: releasing somebody else's latch reverts a live wearer's speed.
void release();

/// Is a session's project latched? (What `vr.locomotion` reports as the source
/// of its defaults, and what a test asserts the adoption rule with.)
bool adopted();

/// A SESSION OVERRIDE for one row, by id and by the row's own number
/// (`validate` produced it). Writes nothing to the document.
void override(const QString &rowId, double value);
/// Which rows the session has overridden, in table order.
QStringList overridden();
/// Drops every override (and what a test calls to start clean).
void clearOverrides();

}   // namespace vrworld

#endif   // VRWORLD_H
