/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// services.clipboard — the clipboard's two LEAVES (CLIPBOARD_SPEC P0):
// the envelope codec (src/io/clipboardformat.h) and the key-aware asset walk
// (src/io/assetrefs.h), plus the in-memory backend the headless sessions use.
//
// Why these two and not the service: the codec and the walk are where a
// clipboard is right or wrong on its own terms — a payload that does not round
// trip, or a reference key the walk does not know, is a silent data loss no
// integration test would attribute correctly. The service, the resolver and the
// paste run against a real database and a real project, so they are gated by
// scripting.e2e.clipboard / _xproject / _assets, through the verbs.
//
// THE LOAD-BEARING ASSERTION here is materialAssetKeys(): it is derived from
// iris::PbrMaterial::mapRowNames(), so a new texture slot added to the material
// model joins the closure walk automatically instead of being forgotten. The
// list is asserted against the model's own, which is what makes that a contract
// and not a coincidence.
//
// No display, no database, no engine: QCoreApplication-level work.

#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdio>

#include "irisgl/document/materials/pbrmaterial.h"
#include "io/assetrefs.h"
#include "io/clipboardformat.h"
#include "services/clipboardbackend.h"

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) { std::printf("PASS %s\n", name); } \
    else { std::printf("FAIL %s\n", name); ++failures; } \
} while (0)

using namespace clipboardformat;

namespace {

const char *kMeshGuid    = "11111111-1111-1111-1111-111111111111";
const char *kTextureGuid = "22222222-2222-2222-2222-222222222222";
const char *kNormalGuid  = "33333333-3333-3333-3333-333333333333";
const char *kIesGuid     = "44444444-4444-4444-4444-444444444444";
const char *kClipGuid    = "55555555-5555-5555-5555-555555555555";
const char *kAvatarGuid  = "66666666-6666-6666-6666-666666666666";
const char *kDecalGuid   = "77777777-7777-7777-7777-777777777777";
const char *kRampGuid    = "88888888-8888-8888-8888-888888888888";
const char *kShaderGuid  = "99999999-9999-9999-9999-999999999999";
const char *kNodeGuid    = "aaaaaaaa-1111-1111-1111-111111111111";
const char *kChildGuid   = "aaaaaaaa-2222-2222-2222-222222222222";
const char *kOwnerGuid   = "aaaaaaaa-3333-3333-3333-333333333333";
const char *kFocusGuid   = "aaaaaaaa-4444-4444-4444-444444444444";
const char *kBuiltinGuid = "00000000-0000-0000-0000-000000000005";

/// A node object shaped exactly like SceneWriter's, carrying one value in every
/// reference key the table knows — plus three values that must NOT be collected
/// (a builtin `:` mesh path on the child, a reserved guid, a project-relative
/// texture path).
QJsonObject sampleNodeObject()
{
    QJsonObject material;
    QJsonObject values;
    values[QStringLiteral("baseColorMap")] = QString::fromLatin1(kTextureGuid);
    values[QStringLiteral("normalMap")] = QString::fromLatin1(kNormalGuid);
    // NOT a guid: a texture that is not a catalog asset is written as a
    // project-relative path, and the walk must leave it alone.
    values[QStringLiteral("roughnessMap")] = QStringLiteral("Textures/rough.png");
    values[QStringLiteral("customPieceGraph")] = QString::fromLatin1(kShaderGuid);
    values[QStringLiteral("customPiece")] = QStringLiteral("a3f9e2.piece");
    material[QStringLiteral("values")] = values;
    material[QStringLiteral("materialType")] = QStringLiteral("pbr");

    QJsonObject skel;
    skel[QStringLiteral("guid")] = QString::fromLatin1(kClipGuid);
    skel[QStringLiteral("source")] = QStringLiteral("clips/walk.dae");
    QJsonObject anim;
    anim[QStringLiteral("name")] = QStringLiteral("Walk");
    anim[QStringLiteral("skeletalAnimation")] = skel;
    QJsonArray animations;
    animations.append(anim);

    QJsonObject constraint;
    constraint[QStringLiteral("constraintFrom")] = QString::fromLatin1(kNodeGuid);
    constraint[QStringLiteral("constraintTo")] = QString::fromLatin1(kOwnerGuid);
    QJsonArray constraints;
    constraints.append(constraint);
    QJsonObject physics;
    physics[QStringLiteral("constraints")] = constraints;

    QJsonObject attachment;
    attachment[QStringLiteral("owner")] = QString::fromLatin1(kOwnerGuid);
    attachment[QStringLiteral("socket")] = QStringLiteral("Head");

    QJsonObject avatar;
    avatar[QStringLiteral("asset")] = QString::fromLatin1(kAvatarGuid);

    QJsonObject child;
    child[QStringLiteral("guid")] = QString::fromLatin1(kChildGuid);
    child[QStringLiteral("name")] = QStringLiteral("Emitter");
    child[QStringLiteral("type")] = QStringLiteral("particle system");
    child[QStringLiteral("texture")] = QString::fromLatin1(kDecalGuid);
    child[QStringLiteral("colourRampGuid")] = QString::fromLatin1(kRampGuid);
    child[QStringLiteral("iesProfile")] = QString::fromLatin1(kIesGuid);
    // A BUILTIN primitive: `:`-prefixed, never a catalog asset.
    child[QStringLiteral("mesh")] = QStringLiteral(":/models/plane.obj");
    child[QStringLiteral("focusTarget")] = QString::fromLatin1(kFocusGuid);
    // A RESERVED guid (a builtin shader/preset id): excluded by rule.
    child[QStringLiteral("lightTexture")] = QString::fromLatin1(kBuiltinGuid);
    child[QStringLiteral("children")] = QJsonArray();

    QJsonObject node;
    node[QStringLiteral("guid")] = QString::fromLatin1(kNodeGuid);
    node[QStringLiteral("name")] = QStringLiteral("Cube");
    node[QStringLiteral("type")] = QStringLiteral("mesh");
    node[QStringLiteral("mesh")] = QString::fromLatin1(kMeshGuid);
    node[QStringLiteral("material")] = material;
    node[QStringLiteral("animations")] = animations;
    node[QStringLiteral("physicsProperties")] = physics;
    node[QStringLiteral("socketAttachment")] = attachment;
    node[QStringLiteral("avatar")] = avatar;
    QJsonArray children;
    children.append(child);
    node[QStringLiteral("children")] = children;
    return node;
}

void testAssetRefs()
{
    const QJsonObject node = sampleNodeObject();
    const QStringList guids = assetrefs::collectAssetGuids(node);

    const QStringList expected = { kMeshGuid, kTextureGuid, kNormalGuid, kShaderGuid,
                                   kClipGuid, kAvatarGuid, kDecalGuid, kRampGuid, kIesGuid };
    bool all = true;
    for (const QString &guid : expected) if (!guids.contains(guid)) { all = false; break; }
    CHECK(all, "every asset key in the table is collected (mesh, both maps, shader piece, "
               "skeletal clip, avatar, particle texture + ramp, IES)");
    CHECK(guids.size() == expected.size(),
          "and NOTHING else: no node guids, no builtin ':' mesh, no relative texture path, "
          "no reserved guid");
    CHECK(!guids.contains(QString::fromLatin1(kBuiltinGuid)),
          "a reserved builtin guid never joins a closure (its ids collide across types)");
    CHECK(!guids.contains(QString::fromLatin1(kOwnerGuid)),
          "a socket owner is a NODE guid, not an asset");

    // The refs carry the key path, which is what the missing report shows.
    const auto refs = assetrefs::collectAssetRefs(node);
    QString meshKey, mapKey, clipKey;
    for (const auto &ref : refs) {
        if (ref.guid == QLatin1String(kMeshGuid)) meshKey = ref.key;
        if (ref.guid == QLatin1String(kTextureGuid)) mapKey = ref.key;
        if (ref.guid == QLatin1String(kClipGuid)) clipKey = ref.key;
    }
    CHECK(meshKey == QLatin1String("mesh"), "the mesh ref names its key");
    CHECK(mapKey == QLatin1String("material.values.baseColorMap"),
          "a texture ref names its material slot");
    CHECK(clipKey == QLatin1String("animations[0].skeletalAnimation.guid"),
          "a clip ref names its animation index");

    // ---- the guard that matters: the table IS the material model's ---------
    const QStringList materialKeys = assetrefs::materialAssetKeys();
    bool everyRow = true;
    for (const QString &row : iris::PbrMaterial::mapRowNames())
        if (!materialKeys.contains(row)) { everyRow = false; break; }
    CHECK(everyRow, "materialAssetKeys covers EVERY PbrMaterial map row — a new texture slot "
                    "joins the closure walk without a second spelling");
    CHECK(materialKeys.contains(QStringLiteral("customPieceGraph")),
          "and the generated-piece shader asset, which is not a Property row");

    // ---- asset remap: by key, and only the mapped values -------------------
    QHash<QString, QString> map;
    map.insert(QString::fromLatin1(kTextureGuid), QStringLiteral("new-texture"));
    map.insert(QString::fromLatin1(kMeshGuid), QStringLiteral("new-mesh"));
    map.insert(QString::fromLatin1(kOwnerGuid), QStringLiteral("must-not-happen"));
    QJsonObject remapped = node;
    const int rewritten = assetrefs::remapAssetGuids(remapped, map);
    CHECK(rewritten == 2, "exactly the two mapped ASSET values were rewritten");
    CHECK(remapped.value(QStringLiteral("mesh")).toString() == QLatin1String("new-mesh"),
          "the mesh guid moved");
    CHECK(remapped.value(QStringLiteral("material")).toObject()
                  .value(QStringLiteral("values")).toObject()
                  .value(QStringLiteral("baseColorMap")).toString() == QLatin1String("new-texture"),
          "the texture slot moved");
    CHECK(remapped.value(QStringLiteral("socketAttachment")).toObject()
                  .value(QStringLiteral("owner")).toString() == QLatin1String(kOwnerGuid),
          "a NODE guid is never touched by the asset remap, even when the map names it");

    // ---- node remap: the four node-guid slots, including the two that were
    //      the recorded gap (constraint endpoints, camera focus target) -------
    QHash<QString, QString> nodeMap;
    nodeMap.insert(QString::fromLatin1(kNodeGuid), QStringLiteral("fresh-node"));
    nodeMap.insert(QString::fromLatin1(kOwnerGuid), QStringLiteral("fresh-owner"));
    nodeMap.insert(QString::fromLatin1(kFocusGuid), QStringLiteral("fresh-focus"));
    QJsonObject nodeRemapped = node;
    const int nodeRewrites = assetrefs::remapNodeGuids(nodeRemapped, nodeMap);
    CHECK(nodeRewrites == 5, "own guid + socket owner + both constraint endpoints + focus target "
                             "were re-pointed (5 values here; the child's own guid was not in "
                             "the map)");
    CHECK(nodeRemapped.value(QStringLiteral("physicsProperties")).toObject()
                      .value(QStringLiteral("constraints")).toArray().at(0).toObject()
                      .value(QStringLiteral("constraintTo")).toString()
              == QLatin1String("fresh-owner"),
          "a physics constraint endpoint follows the copy (the gap shared with Duplicate)");
    CHECK(nodeRemapped.value(QStringLiteral("children")).toArray().at(0).toObject()
                      .value(QStringLiteral("focusTarget")).toString()
              == QLatin1String("fresh-focus"),
          "a camera focus target follows the copy");
    CHECK(nodeRemapped.value(QStringLiteral("mesh")).toString() == QLatin1String(kMeshGuid),
          "and an ASSET guid is never touched by the node remap");

    CHECK(assetrefs::isGuidValue(QString::fromLatin1(kMeshGuid)), "a bare UUID reads as a guid");
    CHECK(!assetrefs::isGuidValue(QStringLiteral("Textures/rough.png")), "a path does not");
    CHECK(!assetrefs::isGuidValue(QStringLiteral(":/models/plane.obj")), "a builtin path does not");
}

void testEnvelope()
{
    Envelope envelope;
    envelope.app = QStringLiteral("0.9.1b");
    envelope.sceneFormat = 2;
    envelope.created = QStringLiteral("2026-09-10T09:00:00Z");
    envelope.source.storeId = QStringLiteral("store-a");
    envelope.source.projectGuid = QStringLiteral("project-a");
    envelope.source.storeRoot = QStringLiteral("/tmp/store-a");

    ClipItem nodeItem;
    nodeItem.kind = QLatin1String(kind::node());
    nodeItem.data = QJsonObject{ { QStringLiteral("node"), sampleNodeObject() },
                                 { QStringLiteral("parent"), QStringLiteral("root-guid") },
                                 { QStringLiteral("index"), 3 } };
    envelope.items.append(nodeItem);

    // A kind this build does not know — a newer build's item. It must survive a
    // read (so `clipboard.text()` still round-trips it) and be reported, never
    // refuse the whole payload.
    ClipItem futureItem;
    futureItem.kind = QStringLiteral("decal2");
    futureItem.data = QJsonObject{ { QStringLiteral("name"), QStringLiteral("from the future") } };
    envelope.items.append(futureItem);

    ClipAsset asset;
    asset.guid = QString::fromLatin1(kTextureGuid);
    asset.name = QStringLiteral("brick.png");
    asset.type = QStringLiteral("texture");
    asset.typeId = 2;
    asset.parent = QStringLiteral("parent-guid");
    asset.viewFilter = 2;
    asset.dependencies = QStringList{ QString::fromLatin1(kNormalGuid) };
    ClipFile file;
    file.role = QStringLiteral("source");
    file.name = QStringLiteral("brick.png");
    file.ext = QStringLiteral("png");
    file.oid = QStringLiteral("deadbeef");
    file.inlineData = QByteArray("\x89PNG\r\n\x1a\n binary \x00 bytes", 26);
    file.size = file.inlineData.size();
    asset.files.append(file);
    asset.blob = QByteArray("{\"node\":true}");
    envelope.assets.insert(asset.guid, asset);

    const QByteArray text = envelope.toText();
    CHECK(!text.contains('\n'), "the payload is ONE LINE (it has to survive a chat window)");
    CHECK(text.startsWith("{\"format\":\"jahshaka.clipboard\",\"version\":1"),
          "the payload LEADS with its format marker and version — a human pasting it into a "
          "text editor sees what it is, and the sniff costs a prefix compare");
    CHECK(Envelope::looksLikeEnvelope(text), "and the cheap sniff recognises it");
    CHECK(!Envelope::looksLikeEnvelope(QByteArray("Dear Bob, here is the scene we discussed.")),
          "prose is not mistaken for a payload");
    CHECK(!Envelope::looksLikeEnvelope(QByteArray("{\"format\":\"jahshaka.scene\",\"nodes\":[]}")),
          "another Jahshaka format is not mistaken for one either");

    QString error;
    const Envelope read = Envelope::fromText(text, &error);
    CHECK(!read.isNull() && error.isEmpty(), "the payload parses back");
    CHECK(read.version == kVersion && read.sceneFormat == 2, "envelope + scene versions survive");
    CHECK(read.app == QLatin1String("0.9.1b"), "the writing build is recorded");
    CHECK(read.source.storeId == QLatin1String("store-a") &&
          read.source.storeRoot == QLatin1String("/tmp/store-a"),
          "the source library identity and its same-machine root hint survive");
    CHECK(read.items.size() == 2, "both items survive, including the unknown kind");
    CHECK(read.itemsOfKind(QLatin1String(kind::node())).size() == 1, "the node item is findable");
    CHECK(read.items.at(0).nodeObject().value(QStringLiteral("name")).toString()
              == QLatin1String("Cube"),
          "the node object round-trips verbatim");
    CHECK(read.items.at(0).siblingIndex() == 3 &&
          read.items.at(0).parentGuid() == QLatin1String("root-guid"),
          "the fragment's anchor round-trips");
    CHECK(read.items.at(1).kind == QLatin1String("decal2"),
          "an item kind this build has never heard of is preserved, not dropped");

    CHECK(read.assets.size() == 1, "the closure survives");
    const ClipAsset back = read.assets.value(QString::fromLatin1(kTextureGuid));
    CHECK(back.name == QLatin1String("brick.png") && back.typeId == 2 && back.viewFilter == 2 &&
          back.parent == QLatin1String("parent-guid"),
          "the catalog row's identity, type and placement survive");
    CHECK(back.dependencies == QStringList{ QString::fromLatin1(kNormalGuid) },
          "its dependency edges survive");
    CHECK(back.files.size() == 1 && back.files.at(0).oid == QLatin1String("deadbeef"),
          "the file's content id survives");
    CHECK(back.files.at(0).inlineData == file.inlineData,
          "and the BYTES survive base64 verbatim — including the NULs a texture contains");
    CHECK(back.blob == QByteArray("{\"node\":true}"), "the row's stored blob survives");
    CHECK(read.inlineBytes() == file.inlineData.size(), "the inline total is reported");

    // ---- tolerance ---------------------------------------------------------
    QJsonObject raw = QJsonDocument::fromJson(text).object();
    raw[QStringLiteral("version")] = 2;                       // a NEWER envelope
    raw[QStringLiteral("somethingNew")] = QStringLiteral("x");
    const Envelope newer = Envelope::fromText(QJsonDocument(raw).toJson(QJsonDocument::Compact));
    CHECK(!newer.isNull() && newer.items.size() == 2,
          "a NEWER envelope version still yields its known items (forward tolerance)");

    QString junkError;
    CHECK(Envelope::fromText(QByteArray("not json at all"), &junkError).isNull() &&
          !junkError.isEmpty(),
          "junk is refused with a reason, never an exception");
    CHECK(Envelope::fromText(QByteArray("{\"format\":\"jahshaka.scene\"}")).isNull(),
          "a different format is refused");
    QString damagedError;
    CHECK(Envelope::fromText(QByteArray("{\"format\":\"jahshaka.clipboard\",\"version\""),
                             &damagedError).isNull() &&
          damagedError.contains(QLatin1String("damaged")),
          "a TRUNCATED payload (a chat window that wrapped it) says it is damaged, which is a "
          "different problem from 'that is not one of ours'");
    CHECK(Envelope::fromText(QByteArray("{\"format\":\"jahshaka.clipboard\",\"version\":1,"
                                        "\"items\":[]}")).isNull(),
          "an empty payload is not a clipboard");
}

void testBackend()
{
    MemoryClipboardBackend backend;
    CHECK(backend.payload().isEmpty(), "a fresh in-memory backend is empty");
    backend.setPayload(QByteArray("payload"));
    CHECK(backend.payload() == QByteArray("payload"), "and holds what it was given");
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    testAssetRefs();
    testEnvelope();
    testBackend();
    if (failures == 0) std::printf("services.clipboard: all checks passed\n");
    else               std::printf("services.clipboard: %d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
