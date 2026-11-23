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
//     reference), for the same values; the avatar definition and the locomotion asset the same way through their own
//     QJsonDocument round trip (written, parsed, read, written again: the same bytes).
// CONTROLS: fragments checked > 0; texts compared > 0.
// CPU only: the document's own types, no engine, no display. Qt6::Core is the reference and the edge.
#include "app/qtstd.h"
#include "irisgl/document/assets/avatardefinition.h"
#include "irisgl/document/animation/locomotion.h"
#include "irisgl/document/scenegraph/scene.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <cstdio>
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
    if (!c.weatherMapGuid.isEmpty()) o.insert("weatherMap", c.weatherMapGuid);
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
        if (i % 3 == 0) c.weatherMapGuid = QStringLiteral("weather-%1").arg(i);
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
    def.name = QStringLiteral("Avatar é");
    def.modelAsset = QStringLiteral("model-guid");
    def.rig.rigId = QStringLiteral("rig");
    def.rig.bones = 3;
    def.rig.boneNames = { QStringLiteral("hips"), QStringLiteral("spine"), QStringLiteral("head") };
    def.rig.hasUnitScale = true;
    def.rig.unitScale = 0.01;
    iris::AvatarClipEntry clip;
    clip.asset = QStringLiteral("clip-guid");
    clip.rawName = QStringLiteral("Take 001");
    clip.name = QStringLiteral("walk");
    def.clips.append(clip);
    def.defaultClip = QStringLiteral("walk");
    def.hasMovement = true;
    def.movement.walkSpeed = 2.5f;
    def.hasSockets = true;
    def.sockets = iris::Value(iris::Value::Object{{"hand_r", iris::Value(iris::Value::Object{{"bone", iris::Value("hand")}})}});
    def.hasLocomotion = true;
    {
        iris::LocomotionAsset &a = def.locomotion;
        a.name = QStringLiteral("Biped");
        a.entry = QStringLiteral("idle");
        iris::LocomotionStateDef idle;
        idle.name = QStringLiteral("idle");
        idle.kind = iris::LocomotionSourceKind::Clip;
        idle.clip = QStringLiteral("walk");
        idle.speed = 1.25f;
        iris::LocomotionStateDef move;
        move.name = QStringLiteral("move");
        move.kind = iris::LocomotionSourceKind::BlendSpace;
        move.space.syncClips = false;
        iris::LocomotionBlendSample sample;
        sample.clip = QStringLiteral("walk");
        sample.position = 2.0f;
        sample.authoredSpeed = 1.5f;
        move.space.samples.append(sample);
        sample.position = 5.5f;
        sample.authoredSpeed = 4.75f;
        move.space.samples.append(sample);   // a blend space needs two
        a.states = { idle, move };
        iris::LocomotionTransitionDef t;
        t.from = QStringLiteral("idle");
        t.to = QStringLiteral("move");
        QString err;
        iris::LocomotionCondition::parse(QStringLiteral("speed > 0.5"), t.condition, &err);
        t.blendDuration = 0.2f;
        a.transitions = { t };
    }
    const QByteArray first = QJsonDocument(qtstd::toQJsonObject(iris::avatarDefinitionToJson(def))).toJson();
    iris::AvatarDefinition back;
    QString error;
    const bool read = iris::avatarDefinitionFromJson(qtstd::fromQJsonObject(QJsonDocument::fromJson(first).object()), back, &error);
    const QByteArray second = QJsonDocument(qtstd::toQJsonObject(iris::avatarDefinitionToJson(back))).toJson();
    CHECK(read && first == second, "the avatar definition: its text read back writes the same bytes (" + error.toStdString() + ")");
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

int main()
{
    testFragments();
    testFileCodecs();
    std::printf("\nfragments checked %d, texts compared %d\n", fragments, texts);
    CHECK(fragments > 0 && texts > 0, "control: fragments and texts > 0");
    if (failures) std::printf("document.fragment_codecs: FAILED (%d)\n", failures);
    else std::printf("document.fragment_codecs: PASSED\n");
    return failures ? 1 : 0;
}
