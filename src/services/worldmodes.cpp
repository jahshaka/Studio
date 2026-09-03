/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/worldmodes.h"

#include "irisgl/document/scenegraph/scene.h"

#include <QJsonValue>

namespace worldmodes {

namespace {

/// The planar-reflection budget per tier, shared by the row below and by the
/// resolver beside it (which cannot call rows() — it runs while rows() is being
/// built). Low, Medium, High, Epic.
const int kPlanarTier[4] = { 0, 0, 1, 2 };

/// The row's `get`. iris::Scene::planarReflectionBudget carries -1 for "never
/// set, follow the mode" — the one negative value the field can hold — and the
/// registry's contract is that a row's get() returns the RESOLVED value. Every
/// path that applies a mode writes a concrete number through, so -1 only ever
/// survives in a Custom-mode scene or one loaded from a document written before
/// the feature existed; both resolve to "off", which is also exactly what
/// SceneMirror does with a negative budget.
int planarBudgetOf(const iris::ScenePtr &s)
{
    if (!s) return 0;
    if (s->planarReflectionBudget >= 0) return s->planarReflectionBudget;
    const int m = s->worldMode;
    return (m >= 0 && m <= 3) ? kPlanarTier[m] : 0;
}

/// The four tier columns, in order: Low, Medium, High, Epic.
/// Values and reasons come from POST_CHAIN_SPEC.md §9.3 — the effect rows it
/// also lists (HDR, bloom, SSAO, SMAA, SSR, refraction) are NOT here: they have
/// no engine behind them yet and a row that silently does nothing is worse than
/// no row. They land with their phases, in this table, and nothing else changes.
QVector<Row> buildRows()
{
    QVector<Row> out;

    // ---- Rendering ---------------------------------------------------------
    {
        Row r;
        r.id = QStringLiteral("msaa");
        r.label = QStringLiteral("Anti-Aliasing (MSAA)");
        r.group = QStringLiteral("Rendering");
        r.type = RowType::Enum;
        r.options = { { QStringLiteral("off"), QStringLiteral("Off"), 1 },
                      { QStringLiteral("2x"),  QStringLiteral("2x"),  2 },
                      { QStringLiteral("4x"),  QStringLiteral("4x"),  4 },
                      { QStringLiteral("8x"),  QStringLiteral("8x"),  8 } };
        r.tier[0] = 1; r.tier[1] = 1; r.tier[2] = 2; r.tier[3] = 4;
        r.cost = QStringLiteral("Hardware edge smoothing. Costs render-target memory and "
                                "bandwidth in proportion to the sample count; the driver may "
                                "clamp the request.");
        r.get = [](const iris::ScenePtr &s) { return s->antiAliasing; };
        r.set = [](const iris::ScenePtr &s, int v) { s->antiAliasing = v; };
        out.append(r);
    }

    // ---- Shadows -----------------------------------------------------------
    {
        Row r;
        r.id = QStringLiteral("shadowResolution");
        r.label = QStringLiteral("Shadow Map Size");
        r.group = QStringLiteral("Shadows");
        r.type = RowType::Enum;
        r.options = { { QStringLiteral("auto"), QStringLiteral("Auto"), 0 },
                      { QStringLiteral("512"),  QStringLiteral("512"),  512 },
                      { QStringLiteral("1024"), QStringLiteral("1024"), 1024 },
                      { QStringLiteral("2048"), QStringLiteral("2048"), 2048 },
                      { QStringLiteral("4096"), QStringLiteral("4096"), 4096 } };
        r.tier[0] = 512; r.tier[1] = 1024; r.tier[2] = 2048; r.tier[3] = 4096;
        r.cost = QStringLiteral("One shadow atlas for the whole scene, R wide by 3.5R tall at "
                                "32-bit depth: 512 costs ~3.6 MB, 1024 ~14 MB, 2048 ~59 MB, "
                                "4096 ~235 MB of VRAM.");
        r.get = [](const iris::ScenePtr &s) { return s->shadowResolution; };
        r.set = [](const iris::ScenePtr &s, int v) { s->shadowResolution = v; };
        out.append(r);
    }
    {
        Row r;
        r.id = QStringLiteral("shadowFilter");
        r.label = QStringLiteral("Shadow Softness");
        r.group = QStringLiteral("Shadows");
        r.type = RowType::Enum;
        r.options = { { QStringLiteral("auto"),     QStringLiteral("Auto"),      -1 },
                      { QStringLiteral("hard"),     QStringLiteral("Hard"),       0 },
                      { QStringLiteral("soft"),     QStringLiteral("Soft"),       1 },
                      { QStringLiteral("verysoft"), QStringLiteral("Very Soft"),  2 } };
        r.tier[0] = 0; r.tier[1] = 1; r.tier[2] = 1; r.tier[3] = 2;
        r.cost = QStringLiteral("PCF filter width for every shadowed light: Hard 2x2, Soft 4x4, "
                                "Very Soft 6x6 taps. Auto uses the softest quality any light in "
                                "the scene asked for. Takes effect next frame; no rebuild.");
        r.get = [](const iris::ScenePtr &s) { return s->shadowFilterTier; };
        r.set = [](const iris::ScenePtr &s, int v) { s->shadowFilterTier = v; };
        out.append(r);
    }

    // ---- Global illumination ----------------------------------------------
    {
        Row r;
        r.id = QStringLiteral("giMode");
        r.label = QStringLiteral("Global Illumination");
        r.group = QStringLiteral("Global Illumination");
        r.type = RowType::Enum;
        r.options = { { QStringLiteral("off"),              QStringLiteral("Off"),               0 },
                      { QStringLiteral("instant_radiosity"), QStringLiteral("Instant Radiosity"), 1 },
                      { QStringLiteral("vct"),              QStringLiteral("VCT"),               2 },
                      { QStringLiteral("vct_pcc_hybrid"),   QStringLiteral("VCT + Probes"),      3 } };
        r.tier[0] = 0; r.tier[1] = 0; r.tier[2] = 1; r.tier[3] = 2;
        r.cost = QStringLiteral("Indirect light. Instant Radiosity re-traces on light moves; VCT "
                                "re-voxelizes on geometry edits (editing latency, not frame time). "
                                "VCT + Probes also bakes six renders per reflection probe.");
        r.get = [](const iris::ScenePtr &s) { return int(s->giMode); };
        r.set = [](const iris::ScenePtr &s, int v) { s->giMode = iris::GiMode(v); };
        out.append(r);
    }
    {
        Row r;
        r.id = QStringLiteral("giQuality");
        r.label = QStringLiteral("GI Quality");
        r.group = QStringLiteral("Global Illumination");
        r.type = RowType::Enum;
        r.options = { { QStringLiteral("low"),    QStringLiteral("Low"),    0 },
                      { QStringLiteral("medium"), QStringLiteral("Medium"), 1 },
                      { QStringLiteral("high"),   QStringLiteral("High"),   2 } };
        r.tier[0] = 0; r.tier[1] = 0; r.tier[2] = 0; r.tier[3] = 1;
        r.cost = QStringLiteral("Ray/voxel budget. High is a re-solve-latency trap in an editor: "
                                "every geometry or light edit pays for it again.");
        r.get = [](const iris::ScenePtr &s) { return int(s->giQuality); };
        r.set = [](const iris::ScenePtr &s, int v) { s->giQuality = iris::GiQuality(v); };
        out.append(r);
    }

    // ---- Sky ---------------------------------------------------------------
    {
        Row r;
        r.id = QStringLiteral("skyBakeResolution");
        r.label = QStringLiteral("Sky Detail");
        r.group = QStringLiteral("Sky");
        r.type = RowType::Enum;
        r.options = { { QStringLiteral("256"),  QStringLiteral("256"),  256 },
                      { QStringLiteral("512"),  QStringLiteral("512"),  512 },
                      { QStringLiteral("1024"), QStringLiteral("1024"), 1024 } };
        r.tier[0] = 256; r.tier[1] = 256; r.tier[2] = 512; r.tier[3] = 1024;
        r.cost = QStringLiteral("Equirect width the analytic sky is CPU-baked at. Costs bake time "
                                "when the sky changes, never frame time.");
        r.get = [](const iris::ScenePtr &s) { return s->skyBakeResolution; };
        r.set = [](const iris::ScenePtr &s, int v) { s->skyBakeResolution = v; };
        out.append(r);
    }
    {
        Row r;
        r.id = QStringLiteral("ambientFromSky");
        r.label = QStringLiteral("Sky Ambient");
        r.group = QStringLiteral("Sky");
        r.type = RowType::Bool;
        r.tier[0] = 1; r.tier[1] = 1; r.tier[2] = 1; r.tier[3] = 1;
        r.cost = QStringLiteral("Ambient light integrated from the live sky instead of the flat "
                                "Ambient Color. One CPU integral per sky change — cheap in every "
                                "tier; it is a row for discoverability, not for performance.");
        r.get = [](const iris::ScenePtr &s) { return s->ambientFromSky ? 1 : 0; };
        r.set = [](const iris::ScenePtr &s, int v) { s->ambientFromSky = v != 0; };
        out.append(r);
    }

    // ---- Reflections -------------------------------------------------------
    // This row was declared here with `available = false` before the engine
    // could serve it; the planar lane filled in get/set and flipped the flag,
    // and not one consumer of this file changed. That is what the contract-row
    // idea was for.
    //
    // Epic is 2, not the 4 this row was drafted with. Each active plane is a
    // FULL extra scene render inside the editor's per-frame loop, which shares
    // the machine with Qt, the scripting host and the MCP server: four planes
    // means five scene renders and an editor that stops feeling interactive on
    // any scene heavier than a sample. Two mirrors facing the camera at once is
    // already an unusual composition, so the visible payoff of 3 and 4 is small.
    // The verb and this row still ACCEPT up to 8 for anyone who wants it, and
    // an artist can force which mirror wins; only the preset is conservative.
    {
        Row r;
        r.id = QStringLiteral("planarBudget");
        r.label = QStringLiteral("Planar Reflections");
        r.group = QStringLiteral("Reflections");
        r.type = RowType::Int;
        r.minValue = 0; r.maxValue = 8;
        r.tier[0] = kPlanarTier[0]; r.tier[1] = kPlanarTier[1];
        r.tier[2] = kPlanarTier[2]; r.tier[3] = kPlanarTier[3];
        r.cost = QStringLiteral("How many mirror planes may re-render the scene. One extra full "
                                "scene render per plane — the most expensive row in the table. "
                                "CHANGING IT REBUILDS SHADERS: the count is baked into the PBR "
                                "shader, not passed as a uniform, so expect a pause on the next "
                                "frame. Planes are placed per object (Properties > Planar "
                                "Reflector); this is only the cap on how many of them render.");
        r.get = [](const iris::ScenePtr &s) { return planarBudgetOf(s); };
        r.set = [](const iris::ScenePtr &s, int v) { s->planarReflectionBudget = v; };
        out.append(r);
    }

    return out;
}

/// The override map is JSON on the document, so values round-trip as doubles.
bool overrideValue(const iris::ScenePtr &scene, const QString &id, int &out)
{
    if (!scene) return false;
    const QJsonValue v = scene->worldOverrides.value(id);
    if (v.isUndefined() || v.isNull()) return false;
    out = v.toInt();
    return true;
}

}   // namespace

const QVector<Row> &rows()
{
    static const QVector<Row> table = buildRows();
    return table;
}

const Row *row(const QString &id)
{
    for (const Row &r : rows())
        if (r.id == id) return &r;
    return nullptr;
}

QString modeName(Mode m)
{
    switch (m) {
    case Mode::Low:    return QStringLiteral("low");
    case Mode::Medium: return QStringLiteral("medium");
    case Mode::High:   return QStringLiteral("high");
    case Mode::Epic:   return QStringLiteral("epic");
    case Mode::Custom: break;
    }
    return QStringLiteral("custom");
}

Mode modeFromName(const QString &name, bool *ok)
{
    const QString n = name.trimmed().toLower();
    if (ok) *ok = true;
    if (n == QLatin1String("low"))    return Mode::Low;
    if (n == QLatin1String("medium")) return Mode::Medium;
    if (n == QLatin1String("high"))   return Mode::High;
    if (n == QLatin1String("epic"))   return Mode::Epic;
    if (n == QLatin1String("custom")) return Mode::Custom;
    if (ok) *ok = false;
    return Mode::Custom;
}

QStringList modeNames()
{
    return { QStringLiteral("low"), QStringLiteral("medium"),
             QStringLiteral("high"), QStringLiteral("epic") };
}

Mode mode(const iris::ScenePtr &scene)
{
    if (!scene) return Mode::Custom;
    const int m = scene->worldMode;
    return (m >= 0 && m <= 3) ? Mode(m) : Mode::Custom;
}

int tierValue(const Row &r, Mode m, const iris::ScenePtr &scene)
{
    if (m == Mode::Custom) return resolved(scene, r);
    return r.tier[int(m)];
}

int resolved(const iris::ScenePtr &scene, const Row &r)
{
    // The write-through invariant: the backing field IS the resolved value.
    if (r.get && scene) return r.get(scene);
    // A row with no backing field yet (a declared contract, §9.2) has nowhere to
    // write through TO, so it resolves the long way: an explicit pin, else the
    // current tier's value, else Epic's as the documented shape of the row.
    int v = 0;
    if (overrideValue(scene, r.id, v)) return v;
    const Mode m = mode(scene);
    return m == Mode::Custom ? r.tier[3] : r.tier[int(m)];
}

QString source(const iris::ScenePtr &scene, const Row &r)
{
    if (!scene) return QStringLiteral("custom");
    if (scene->worldOverrides.contains(r.id)) return QStringLiteral("override");
    return mode(scene) == Mode::Custom ? QStringLiteral("custom") : QStringLiteral("mode");
}

namespace {

/// Refuses a value the row cannot hold: an enum row takes only its listed
/// values, an int row only its range, a bool row only 0/1.
bool validate(const Row &r, int value)
{
    switch (r.type) {
    case RowType::Bool: return value == 0 || value == 1;
    case RowType::Int:  return value >= r.minValue && value <= r.maxValue;
    case RowType::Enum:
        for (const EnumOption &o : r.options)
            if (o.value == value) return true;
        return false;
    }
    return false;
}

/// Write-through: the tier value lands in the backing field. A row with no
/// backing field is SKIPPED, deliberately — inserting its tier value into
/// worldOverrides would mark it "pinned by the user" for ever after, and it
/// would then survive every future mode switch (the bug the panel's `*` marker
/// made visible the first time a mode was applied).
void writeField(const iris::ScenePtr &scene, const Row &r, int value)
{
    if (r.set) r.set(scene, value);
}

}   // namespace

void setMode(const iris::ScenePtr &scene, Mode m)
{
    if (!scene) return;
    scene->worldMode = int(m);
    if (m == Mode::Custom) return;   // no tier to write through
    for (const Row &r : rows()) {
        if (scene->worldOverrides.contains(r.id)) continue;   // pinned: survives the switch
        writeField(scene, r, r.tier[int(m)]);
    }
}

bool setRowValue(const iris::ScenePtr &scene, const QString &id, int value, bool recordOverride)
{
    if (!scene) return false;
    const Row *r = row(id);
    if (!r || !validate(*r, value)) return false;
    writeField(scene, *r, value);
    // A row with no backing field can ONLY be remembered as a pin, so an
    // explicit set of one always records (it is a deliberate user choice —
    // unlike setMode's sweep, which never touches those rows).
    if (recordOverride || !r->set) scene->worldOverrides.insert(id, value);
    return true;
}

bool setRowValueByOptionId(const iris::ScenePtr &scene, const QString &id, const QString &optionId)
{
    const Row *r = row(id);
    if (!r) return false;
    int value = 0;
    if (!valueFromId(*r, optionId, value)) return false;
    return setRowValue(scene, id, value);
}

void pinRowValue(const iris::ScenePtr &scene, const QString &id, int value)
{
    if (!scene || !row(id)) return;
    scene->worldOverrides.insert(id, value);
}

bool clearOverride(const iris::ScenePtr &scene, const QString &id)
{
    if (!scene || !scene->worldOverrides.contains(id)) return false;
    scene->worldOverrides.remove(id);
    const Row *r = row(id);
    const Mode m = mode(scene);
    // Fall back to the tier value. In Custom mode there is no tier, so the
    // field simply keeps whatever it had — dropping the pin is all that happens.
    if (r && m != Mode::Custom) writeField(scene, *r, r->tier[int(m)]);
    return true;
}

void clearOverrides(const iris::ScenePtr &scene)
{
    if (!scene) return;
    const QStringList ids = scene->worldOverrides.keys();
    for (const QString &id : ids) clearOverride(scene, id);
}

QString valueLabel(const Row &r, int value)
{
    if (r.type == RowType::Bool) return value ? QStringLiteral("On") : QStringLiteral("Off");
    for (const EnumOption &o : r.options)
        if (o.value == value) return o.label;
    return QString::number(value);
}

QString valueId(const Row &r, int value)
{
    if (r.type == RowType::Bool) return value ? QStringLiteral("on") : QStringLiteral("off");
    for (const EnumOption &o : r.options)
        if (o.value == value) return o.id;
    return QString::number(value);
}

bool valueFromId(const Row &r, const QString &id, int &out)
{
    const QString n = id.trimmed().toLower();
    if (r.type == RowType::Bool) {
        if (n == QLatin1String("on")  || n == QLatin1String("true")  || n == QLatin1String("1"))
            { out = 1; return true; }
        if (n == QLatin1String("off") || n == QLatin1String("false") || n == QLatin1String("0"))
            { out = 0; return true; }
        return false;
    }
    for (const EnumOption &o : r.options)
        if (o.id == n) { out = o.value; return true; }
    bool ok = false;
    const int v = n.toInt(&ok);
    if (!ok) return false;
    out = v;
    return true;
}

}   // namespace worldmodes
