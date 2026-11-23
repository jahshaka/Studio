// document.fragment_codecs — THE SCENE's JSON FRAGMENTS ON iris::Value WRITE THE BYTES THEY WROTE (NOQT-DOCUMENT-1 stage 4,
// walk 7).
//
// The cloud layer, the height fog, the sun contact shadows and the realistic sky's block are carried as iris::Value
// in the document (scene.cpp's toJson / fromJson, Scene::skyRealisticJson); the editor writes them through
// qtstd::toQJsonValue into the QJsonObject QJsonDocument writes. THE LAWS:
// (1) FIXED POINT: fromJson(toJson(x)) == x, and toJson(fromJson(toJson(x))) == toJson(x), for seeded values of every
//     field (a weather map guid present and absent, each resolution, the ozone flag both ways);
// (2) THE SAME TEXT: QJsonDocument's text of qtstd::toQJsonObject(toJson(x)) - Compact and Indented - equals, byte for
//     byte, the text of the QJsonObject the Qt-typed codec built before this stage (kept HERE, verbatim, as the
//     reference), for the same values - the four fragments, and the avatar definition and the locomotion asset on
//     three definitions (F13); those two also write the same bytes after a read (written, parsed, read, written);
//     and a member PARSED from another writer's text crosses Value and is written back as QJsonDocument wrote it
//     (F10: -0.0, 1e20, 2^53 + 1, a denormal...).
// (3) THE OVERRIDE MAP AS A SET (F12): sameMembers sees the same pins in two orders as equal, a changed or a missing pin
//     as a difference; the world-mode snapshot's map starts as an empty Object.
// (4) THE READERS (the audit's F18): numberOr / boolOr / stringOr / intOr / objectOr / arrayOr equal QJsonValue's own
//     to<T>(d) on the same parsed input, for 13 inputs (every kind a file holds, and an absent member).
// CONTROLS: fragments checked > 0; texts compared > 0; Value == itself DOES see the order (the reason for (3)).
// CPU only: the document's own types, no engine, no display. Qt6::Core is the reference and the edge.
#include "app/qtstd.h"
#include "commands/worldmodecommand.h"
#include "irisgl/document/properties/valueshapes.h"
#include "irisgl/document/assets/avatardefinition.h"
#include "irisgl/document/animation/locomotion.h"
#include "irisgl/document/scenegraph/scene.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdio>
#include <cstring>
#include <string>

static int failures = 0;
static int fragments = 0;
static int texts = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", std::string(msg).c_str()); \
    else { std::printf("FAIL: %s\n", std::string(msg).c_str()); ++failures; } } while (0)

// ---- THE REFERENCE: the Qt-typed codecs as they were at irisgl 4cb65305 (scene.cpp), verbatim but for the names ----
static QJsonObject oldCloud(const iris::CloudLayer &c)
{
    QJsonObject o;
    o.insert("enabled",   c.enabled);
    o.insert("coverage",  double(c.coverage));
    o.insert("density",   double(c.density));
    o.insert("speed",     double(c.speed));
    o.insert("direction", double(c.direction));
    o.insert("altitude",  double(c.altitude));
    o.insert("shadow",    double(c.shadow));
    if (!c.weatherMapGuid.empty()) o.insert("weatherMap", QString::fromStdString(c.weatherMapGuid));
    return o;
}
static QJsonObject oldFog(const iris::HeightFog &h)
{
    QJsonObject o;
    o.insert("enabled",       h.enabled);
    o.insert("density",       double(h.density));
    o.insert("heightFalloff", double(h.heightFalloff));
    o.insert("baseHeight",    double(h.baseHeight));
    o.insert("startDistance", double(h.startDistance));
    return o;
}
static QJsonObject oldContact(const iris::SunContact &c)
{
    QJsonObject o;
    o.insert("enabled", c.enabled);
    o.insert("range", double(c.range));
    o.insert("resolution", QString::fromLatin1(iris::SunContact::resolutionName(c.resolution)));
    return o;
}
static QJsonObject oldRealistic(const iris::SkyRealistic &r)
{
    QJsonObject o;
    o.insert("sunHaze",       double(r.sunHaze));
    o.insert("aerialScale",   double(r.aerialScale));
    o.insert("skyBrightness", double(r.skyBrightness));
    o.insert("groundAlbedo",  double(r.groundAlbedo));
    o.insert("rayleighScale", double(r.rayleighScale));
    o.insert("ozone",         r.ozone);
    return o;
}

// ---- THE REFERENCE for the file codecs (the lead's F13): the base's Qt-typed WRITERS, verbatim from irisgl 4cb65305
// (document/animation/locomotion.cpp locomotionAssetToJson, document/assets/avatardefinition.cpp
// avatarDefinitionToJson; only renamed and `iris::`-qualified) ----
static QJsonObject oldLocomotionToJson(const iris::LocomotionAsset &asset)
{
    QJsonObject out;
    out[QStringLiteral("name")] = QString::fromStdString(asset.name);
    out[QStringLiteral("entry")] = QString::fromStdString(asset.entry);

    QJsonArray states;
    for (const auto &s : asset.states) {
        QJsonObject o;
        o[QStringLiteral("name")] = QString::fromStdString(s.name);
        o[QStringLiteral("loop")] = s.loop;
        QJsonObject src;
        if (s.kind == iris::LocomotionSourceKind::Clip) {
            src[QStringLiteral("kind")] = QStringLiteral("clip");
            src[QStringLiteral("clip")] = QString::fromStdString(s.clip);
            src[QStringLiteral("speed")] = double(s.speed);
        } else {
            src[QStringLiteral("kind")] = QStringLiteral("blendspace");
            src[QStringLiteral("syncClips")] = s.space.syncClips;
            QJsonArray samples;
            for (const auto &sm : s.space.samples) {
                QJsonObject so;
                so[QStringLiteral("clip")] = QString::fromStdString(sm.clip);
                so[QStringLiteral("position")] = double(sm.position);
                so[QStringLiteral("authoredSpeed")] = double(sm.authoredSpeed);
                samples.append(so);
            }
            src[QStringLiteral("samples")] = samples;
        }
        o[QStringLiteral("source")] = src;
        states.append(o);
    }
    out[QStringLiteral("states")] = states;

    QJsonArray transitions;
    for (const auto &t : asset.transitions) {
        QJsonObject o;
        o[QStringLiteral("from")] = QString::fromStdString(t.from);
        o[QStringLiteral("to")] = QString::fromStdString(t.to);
        // The CANONICAL TEXT, not the enum: a file that stored an int would
        // freeze today's enum order, and a human reading the scene JSON would
        // learn nothing. `text()` round-trips through `parse` byte for byte.
        o[QStringLiteral("condition")] = QString::fromStdString(t.condition.text());
        o[QStringLiteral("blendDuration")] = double(t.blendDuration);
        transitions.append(o);
    }
    out[QStringLiteral("transitions")] = transitions;
    return out;
}

static QJsonObject oldAvatarToJson(const iris::AvatarDefinition &def)
{
    QJsonObject obj;
    obj[QStringLiteral("format")] = QStringLiteral("jah-avatar");
    obj[QStringLiteral("version")] = iris::AvatarDefinition::kFormatVersion;
    obj[QStringLiteral("name")] = QString::fromStdString(def.name);

    QJsonObject model;
    model[QStringLiteral("asset")] = QString::fromStdString(def.modelAsset);
    obj[QStringLiteral("model")] = model;

    QJsonObject rig;
    rig[QStringLiteral("rigId")] = QString::fromStdString(def.rig.rigId);
    rig[QStringLiteral("bones")] = def.rig.bones;
    rig[QStringLiteral("boneNames")] = QJsonArray::fromStringList(qtstd::toQStringList(def.rig.boneNames));
    if (def.rig.hasUnitScale) rig[QStringLiteral("unitScale")] = def.rig.unitScale;
    else rig[QStringLiteral("unitScale")] = QJsonValue();   // the SLOT, written as null
    obj[QStringLiteral("rig")] = rig;

    QJsonArray clips;
    for (const auto &clip : def.clips) {
        QJsonObject entry;
        entry[QStringLiteral("asset")] = QString::fromStdString(clip.asset);
        entry[QStringLiteral("rawName")] = QString::fromStdString(clip.rawName);
        entry[QStringLiteral("name")] = QString::fromStdString(clip.name);
        entry[QStringLiteral("looping")] = clip.looping;
        entry[QStringLiteral("rootMotion")] = clip.rootMotion;
        clips.append(entry);
    }
    obj[QStringLiteral("clips")] = clips;

    QJsonObject defaults;
    defaults[QStringLiteral("activeClip")] = QString::fromStdString(def.defaultClip);
    obj[QStringLiteral("defaults")] = defaults;

    if (def.hasLocomotion) {
        QJsonObject loco;
        QJsonObject roles;
        for (int i = 0; i < iris::kClipRoleCount; ++i) {
            const auto role = iris::ClipRole(i);
            if (def.locomotionRoles.has(role))
                roles[QLatin1String(iris::clipRoleName(role))] = QString::fromStdString(def.locomotionRoles.get(role));
        }
        loco[QStringLiteral("roles")] = roles;
        loco[QStringLiteral("asset")] = oldLocomotionToJson(def.locomotion);
        obj[QStringLiteral("locomotion")] = loco;
    } else {
        obj[QStringLiteral("locomotion")] = QJsonValue();
    }

    if (def.hasMovement) {
        const auto &p = def.movement;
        QJsonObject move;
        move[QStringLiteral("walkSpeed")] = p.walkSpeed;
        move[QStringLiteral("runSpeed")] = p.runSpeed;
        move[QStringLiteral("maxAcceleration")] = p.maxAcceleration;
        move[QStringLiteral("brakingDeceleration")] = p.brakingDeceleration;
        move[QStringLiteral("groundFriction")] = p.groundFriction;
        move[QStringLiteral("jumpVelocity")] = p.jumpVelocity;
        move[QStringLiteral("jumpCount")] = p.jumpCount;
        move[QStringLiteral("coyoteTime")] = p.coyoteTime;
        move[QStringLiteral("jumpReArm")] = p.jumpReArm;
        move[QStringLiteral("airControl")] = p.airControl;
        move[QStringLiteral("gravityScale")] = p.gravityScale;
        move[QStringLiteral("maxStepHeight")] = p.maxStepHeight;
        move[QStringLiteral("walkableFloorAngle")] = p.walkableFloorAngle;
        move[QStringLiteral("orientRotationToMovement")] = p.orientRotationToMovement;
        move[QStringLiteral("rotationRate")] = p.rotationRate;
        move[QStringLiteral("capsuleAuto")] = p.capsuleAuto;
        move[QStringLiteral("capsuleRadius")] = p.capsuleRadius;
        move[QStringLiteral("capsuleHeight")] = p.capsuleHeight;
        obj[QStringLiteral("movement")] = move;
    } else {
        obj[QStringLiteral("movement")] = QJsonValue();
    }

    obj[QStringLiteral("sockets")] = def.hasSockets ? QJsonValue(qtstd::toQJsonObject(def.sockets)) : QJsonValue();   // (the field is a Value now)
    obj[QStringLiteral("blendshapes")] =
        def.hasBlendshapes ? QJsonValue(qtstd::toQJsonObject(def.blendshapes)) : QJsonValue();
    return obj;
}

/// The text the editor writes for `v` (QJsonDocument through the edge) against the reference's, both forms.
static void sameText(const std::string &what, const iris::Value &v, const QJsonObject &reference)
{
    const QJsonDocument ours(qtstd::toQJsonObject(v));
    const QJsonDocument old(reference);
    CHECK(ours.toJson(QJsonDocument::Compact) == old.toJson(QJsonDocument::Compact) &&
          ours.toJson(QJsonDocument::Indented) == old.toJson(QJsonDocument::Indented),
          what + ": the same bytes as the Qt-typed codec wrote (" + old.toJson(QJsonDocument::Compact).toStdString() + ")");
    ++texts;
}

/// A seeded float in [lo, hi): the same sequence on every run.
static float seeded(unsigned &state, float lo, float hi)
{
    state = state * 1664525u + 1013904223u;
    return lo + (hi - lo) * float(state >> 8) / float(1u << 24);
}

static void testFragments()
{
    std::printf("\n-- (1) and (2): the cloud layer, the height fog, the sun contact, the realistic sky --\n");
    unsigned seed = 20261123u;
    for (int i = 0; i < 8; ++i) {
        iris::CloudLayer c;
        c.enabled = (i % 2) == 0;
        c.coverage = seeded(seed, 0.0f, 1.0f);
        c.density = seeded(seed, 0.0f, 4.0f);
        c.speed = float(i * 3);                       // an integral float: QJsonValue stores it as an integer
        c.direction = seeded(seed, 0.0f, 360.0f);
        c.altitude = seeded(seed, 500.0f, 8000.0f);
        c.shadow = seeded(seed, 0.0f, 1.0f);
        if (i % 3 == 0) c.weatherMapGuid = (QStringLiteral("weather-%1").arg(i)).toStdString();
        c = iris::CloudLayer::clamped(c);
        const iris::Value v = c.toJson();
        CHECK(iris::CloudLayer::fromJson(v) == c && iris::CloudLayer::fromJson(v).toJson() == v,
              "CloudLayer " + std::to_string(i) + ": a fixed point");
        sameText("CloudLayer " + std::to_string(i), v, oldCloud(c));
        // ...and through the file: the text read back is the same layer.
        const QJsonObject parsed = QJsonDocument::fromJson(QJsonDocument(qtstd::toQJsonObject(v)).toJson()).object();
        CHECK(iris::CloudLayer::fromJson(qtstd::fromQJsonObject(parsed)) == c, "CloudLayer " + std::to_string(i) + ": read back from its text");
        ++fragments;

        iris::HeightFog h;
        h.enabled = (i % 2) == 1;
        h.density = seeded(seed, 0.0f, 10.0f);
        h.heightFalloff = seeded(seed, 0.001f, 2.0f);
        h.baseHeight = seeded(seed, -10000.0f, 10000.0f);
        h.startDistance = float(i * 100);
        h = iris::HeightFog::clamped(h);
        CHECK(iris::HeightFog::fromJson(h.toJson()) == h && iris::HeightFog::fromJson(h.toJson()).toJson() == h.toJson(),
              "HeightFog " + std::to_string(i) + ": a fixed point");
        sameText("HeightFog " + std::to_string(i), h.toJson(), oldFog(h));
        ++fragments;

        iris::SunContact s;
        s.enabled = (i % 2) == 0;
        s.range = seeded(seed, iris::kSunContactMinRange, iris::kSunContactMaxRange);
        s.resolution = iris::SunContactResolution(i % 3);
        s = iris::SunContact::clamped(s);
        CHECK(iris::SunContact::fromJson(s.toJson()) == s && iris::SunContact::fromJson(s.toJson()).toJson() == s.toJson(),
              "SunContact " + std::to_string(i) + ": a fixed point");
        sameText("SunContact " + std::to_string(i), s.toJson(), oldContact(s));
        ++fragments;

        iris::SkyRealistic r = iris::SkyRealistic::defaults();
        r.sunHaze = seeded(seed, 0.0f, 100.0f);
        r.aerialScale = seeded(seed, 0.0f, 1.0f);
        r.skyBrightness = seeded(seed, 0.0f, 10.0f);
        r.groundAlbedo = seeded(seed, 0.0f, 1.0f);
        r.rayleighScale = float(i);
        r.ozone = (i % 2) == 0;
        r = iris::Scene::clampSkyRealistic(r);
        const iris::Value rv = iris::Scene::skyRealisticJson(r);
        CHECK(iris::Scene::skyRealisticJson(iris::Scene::skyRealisticFromJson(rv)) == rv,
              "SkyRealistic " + std::to_string(i) + ": a fixed point");
        sameText("SkyRealistic " + std::to_string(i), rv, oldRealistic(r));
        ++fragments;
    }
    CHECK(iris::CloudLayer::fromJson(iris::Value(iris::Value::Object{})) == iris::CloudLayer() &&
          iris::HeightFog::fromJson(iris::Value()) == iris::HeightFog() &&
          iris::SunContact::fromJson(iris::Value(iris::Value::Object{})) == iris::SunContact(),
          "an absent block (an empty Object, a Null) reads as the constructor's (the reader-defaults law)");
}

static void testFileCodecs()
{
    std::printf("\n-- (2) the avatar definition and the locomotion asset: written, parsed, read, written again --\n");
    iris::AvatarDefinition def;
    def.name = (QStringLiteral("Avatar é")).toStdString();
    def.modelAsset = (QStringLiteral("model-guid")).toStdString();
    def.rig.rigId = (QStringLiteral("rig")).toStdString();
    def.rig.bones = 3;
    def.rig.boneNames = { "hips", "spine", "head" };
    def.rig.hasUnitScale = true;
    def.rig.unitScale = 0.01;
    iris::AvatarClipEntry clip;
    clip.asset = (QStringLiteral("clip-guid")).toStdString();
    clip.rawName = (QStringLiteral("Take 001")).toStdString();
    clip.name = (QStringLiteral("walk")).toStdString();
    def.clips.push_back(clip);
    def.defaultClip = (QStringLiteral("walk")).toStdString();
    def.hasMovement = true;
    def.movement.walkSpeed = 2.5f;
    def.hasSockets = true;
    def.sockets = iris::Value(iris::Value::Object{{"hand_r", iris::Value(iris::Value::Object{{"bone", iris::Value("hand")}})}});
    def.hasLocomotion = true;
    {
        iris::LocomotionAsset &a = def.locomotion;
        a.name = (QStringLiteral("Biped")).toStdString();
        a.entry = (QStringLiteral("idle")).toStdString();
        iris::LocomotionStateDef idle;
        idle.name = (QStringLiteral("idle")).toStdString();
        idle.kind = iris::LocomotionSourceKind::Clip;
        idle.clip = (QStringLiteral("walk")).toStdString();
        idle.speed = 1.25f;
        iris::LocomotionStateDef move;
        move.name = (QStringLiteral("move")).toStdString();
        move.kind = iris::LocomotionSourceKind::BlendSpace;
        move.space.syncClips = false;
        iris::LocomotionBlendSample sample;
        sample.clip = (QStringLiteral("walk")).toStdString();
        sample.position = 2.0f;
        sample.authoredSpeed = 1.5f;
        move.space.samples.push_back(sample);
        sample.position = 5.5f;
        sample.authoredSpeed = 4.75f;
        move.space.samples.push_back(sample);   // a blend space needs two
        a.states = { idle, move };
        iris::LocomotionTransitionDef t;
        t.from = (QStringLiteral("idle")).toStdString();
        t.to = (QStringLiteral("move")).toStdString();
        std::string err;
        iris::LocomotionCondition::parse("speed > 0.5", t.condition, &err);
        t.blendDuration = 0.2f;
        a.transitions = { t };
    }
    // THE SAME BYTES AS THE BASE's WRITER (F13), on three definitions: the full one, one with every SLOT empty (the
    // nulls), and one with locomotion but no movement or unit scale.
    {
        iris::AvatarDefinition bare;
        bare.name = (QStringLiteral("bare")).toStdString();
        bare.modelAsset = (QStringLiteral("m")).toStdString();
        iris::AvatarDefinition partial = def;
        partial.hasMovement = false;
        partial.rig.hasUnitScale = false;
        partial.hasSockets = false;
        int k = 0;
        for (const iris::AvatarDefinition *d : { &def, &bare, &partial }) {
            sameText("avatar definition " + std::to_string(k), iris::avatarDefinitionToJson(*d), oldAvatarToJson(*d));
            sameText("locomotion asset " + std::to_string(k), iris::locomotionAssetToJson(d->locomotion),
                     oldLocomotionToJson(d->locomotion));
            ++k;
            ++fragments;
        }
    }
    const QByteArray first = QJsonDocument(qtstd::toQJsonObject(iris::avatarDefinitionToJson(def))).toJson();
    iris::AvatarDefinition back;
    std::string error;
    const bool read = iris::avatarDefinitionFromJson(qtstd::fromQJsonObject(QJsonDocument::fromJson(first).object()), back, &error);
    const QByteArray second = QJsonDocument(qtstd::toQJsonObject(iris::avatarDefinitionToJson(back))).toJson();
    CHECK(read && first == second, "the avatar definition: its text read back writes the same bytes (" + error + ")");
    ++texts;
    CHECK(back.name == def.name && back.rig.boneNames == def.rig.boneNames && back.rig.unitScale == def.rig.unitScale &&
          back.sockets == def.sockets && back.movement.walkSpeed == 2.5f && back.locomotion.states.size() == def.locomotion.states.size(),
          "...and holds what was written (names, bones, unit scale, sockets, movement, the locomotion states)");
    ++fragments;

    const iris::LocomotionAsset asset = def.locomotion;
    const QByteArray a1 = QJsonDocument(qtstd::toQJsonObject(iris::locomotionAssetToJson(asset))).toJson();
    iris::LocomotionAsset asset2;
    const bool aread = iris::locomotionAssetFromJson(qtstd::fromQJsonObject(QJsonDocument::fromJson(a1).object()), asset2, &error);
    const QByteArray a2 = QJsonDocument(qtstd::toQJsonObject(iris::locomotionAssetToJson(asset2))).toJson();
    CHECK(aread && a1 == a2 && asset2.states.size() > 0, "the locomotion asset: the same bytes after a read (" +
                                                            std::to_string(asset2.states.size()) + " states)");
    ++texts;
    ++fragments;
}

/// The lead's F10: a member PARSED from a file another writer made crosses Value and is written back by QJsonDocument:
/// the same bytes as QJsonDocument writing the parsed object directly.
static void testParsedNumbers()
{
    std::printf("\n-- F10: parsed numbers through Value and back: the bytes QJsonDocument wrote for the parsed object --\n");
    const QByteArray text = R"({"a":-0.0,"b":1000000.0,"c":1e20,"d":1.5,"e":3,"f":0.1,"g":-0,"h":1e300,"i":9007199254740993,"j":4.9e-324})";
    const QJsonObject parsed = QJsonDocument::fromJson(text).object();
    const QJsonObject crossed = qtstd::toQJsonObject(qtstd::fromQJsonObject(parsed));
    for (auto format : { QJsonDocument::Compact, QJsonDocument::Indented }) {
        const QByteArray direct = QJsonDocument(parsed).toJson(format);
        const QByteArray through = QJsonDocument(crossed).toJson(format);
        std::printf("    direct:  %s\n    through: %s\n", direct.simplified().constData(), through.simplified().constData());
        CHECK(direct == through, std::string("parsed numbers keep their bytes through Value (") +
                                     (format == QJsonDocument::Compact ? "Compact" : "Indented") + ")");
        ++texts;
    }
}

/// The lead's F12: the override map compared as a SET (valueshapes.h sameMembers), as the QJsonObject it replaced was;
/// and the world-mode snapshot's map starts as an EMPTY OBJECT, as the QJsonObject did.
static void testOverrideOrder()
{
    std::printf("\n-- F12: the same pins in another order are the same map; a real difference is not --\n");
    iris::Value a(iris::Value::Object{});
    iris::Value b(iris::Value::Object{});
    CHECK(a.set("giMode", iris::Value(2)) && a.set("ssr", iris::Value(0)), "control: a pin map {giMode, ssr}");
    CHECK(b.set("ssr", iris::Value(0)) && b.set("giMode", iris::Value(2)), "control: the same pins set the other way round");
    CHECK(!(a == b), "control: Value == reads an Object's members IN ORDER (the reason for the set compare)");
    CHECK(iris::sameMembers(a, b), "sameMembers: the same pins in two orders are equal");
    iris::Value c = b;
    CHECK(c.set("ssr", iris::Value(1)) && !iris::sameMembers(a, c), "...a pin's other value is a difference");
    iris::Value d = a;
    CHECK(d.erase("ssr") && !iris::sameMembers(a, d) && !iris::sameMembers(d, a), "...a missing pin is a difference, both ways");
    const iris::Value nested1(iris::Value::Object{{"x", a}, {"y", iris::Value(iris::Value::Array{a, b})}});
    const iris::Value nested2(iris::Value::Object{{"y", iris::Value(iris::Value::Array{b, a})}, {"x", b}});
    CHECK(iris::sameMembers(nested1, nested2), "...recursively, through nested Objects and Arrays (an Array in its order)");
    // F17: with DUPLICATE names one pass plus the size would call these equal ({x, x} against {x, y}).
    const iris::Value dupA(iris::Value::Object{{"x", iris::Value(1)}, {"x", iris::Value(1)}});
    const iris::Value dupB(iris::Value::Object{{"x", iris::Value(1)}, {"y", iris::Value(2)}});
    CHECK(!iris::sameMembers(dupA, dupB) && !iris::sameMembers(dupB, dupA), "...checked BOTH ways: {x, x} is not {x, y}");
    CHECK(WorldModeCommand::Snapshot().overrides.kind() == iris::Kind::Object &&
              WorldModeCommand::Snapshot().overrides.asObject().empty(),
          "a default world-mode snapshot's map is an EMPTY OBJECT (as the QJsonObject was), never a Null");
}

/// The audit's F18: valueshapes.h's six readers set EVERY codec's defaults: each equals QJsonValue's own to<T>(d) - Qt,
/// live, in this process - on the same parsed input, for every kind a file can hold and for an absent member.
static void testReaders()
{
    std::printf("\n-- F18: numberOr / boolOr / stringOr / intOr / objectOr / arrayOr against QJsonValue's own --\n");
    const char *inputs[] = { "3", "3.5", "-0.0", "2147483648", "1e300", "\"3\"", "true", "null", "[]", "{}", "[1,\"a\"]",
                             "{\"x\":1}", nullptr /* absent */ };
    int cases = 0;
    for (const char *in : inputs) {
        const QByteArray text = in ? QByteArray("{\"k\":") + in + "}" : QByteArray("{}");
        const QJsonObject parsed = QJsonDocument::fromJson(text).object();
        const QJsonValue q = parsed.value("k");
        const iris::Value v = qtstd::fromQJsonObject(parsed);
        const std::string what = std::string(in ? in : "(absent)");
        const double n = iris::numberOr(v, "k", 42.5), qn = q.toDouble(42.5);
        CHECK(std::memcmp(&n, &qn, sizeof n) == 0, what + ": numberOr == toDouble(42.5), bit for bit");
        CHECK(iris::boolOr(v, "k", true) == q.toBool(true) && iris::boolOr(v, "k", false) == q.toBool(false),
              what + ": boolOr == toBool(d), both defaults");
        CHECK(iris::stringOr(v, "k", "dflt") == q.toString(QStringLiteral("dflt")).toStdString(),
              what + ": stringOr == toString(\"dflt\")");
        CHECK(iris::intOr(v, "k", -7) == q.toInt(-7), what + ": intOr == toInt(-7) (" + std::to_string(q.toInt(-7)) + ")");
        CHECK(qtstd::toQJsonObject(iris::objectOr(v, "k")) == q.toObject(), what + ": objectOr == toObject()");
        CHECK(qtstd::toQJsonValue(iris::arrayOr(v, "k")).toArray() == q.toArray(), what + ": arrayOr == toArray()");
        ++cases;
    }
    CHECK(cases == 13, "control: 13 inputs, every kind a file can hold and an absent member");
}

int main()
{
    testReaders();
    testOverrideOrder();
    testParsedNumbers();
    testFragments();
    testFileCodecs();
    std::printf("\nfragments checked %d, texts compared %d\n", fragments, texts);
    CHECK(fragments > 0 && texts > 0, "control: fragments and texts > 0");
    if (failures) std::printf("document.fragment_codecs: FAILED (%d)\n", failures);
    else std::printf("document.fragment_codecs: PASSED\n");
    return failures ? 1 : 0;
}
