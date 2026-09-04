/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "irisgl/core/math/qtinterop.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "scripting/modules/nodeapi.h"

#include "scripting/modules/moduleshared.h"
#include "irisgl/document/scenegraph/particlesystemnode.h"
#include "irisgl/document/assets/texture2d.h"
#include "viewport/ieditorviewport.h"
#include "services/planarreflectors.h"
#include "commands/nodeeditcommand.h"
#include "commands/reparentscenenodecommand.h"
#include "commands/setnodepropertycommand.h"
#include "commands/transformscenenodecommand.h"
#include "shell/mainwindow.h"
#include "services/sceneeditservice.h"
#include "data/database/database.h"
#include "services/services.h"
#include "services/undoservice.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/assets/skeleton.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/decalnode.h"
#include "irisgl/document/scenegraph/shadowmap.h"
#include "services/lightbindings.h"

using namespace scriptmod;

QVector<VerbInfo> NodeApi::verbs() const
{
    return {
        { "remove", "node.remove(id) -> bool",
          "Deletes the node (undoable; its DB asset row is only dropped once the delete can no longer be undone).",
          Needs::Document },
        { "duplicate", "node.duplicate(id) -> newId",
          "Duplicates the node under the same parent. Undoable.",
          Needs::Document },
        { "reparent", "node.reparent(id, parentId) -> bool",
          "Moves the node under a new parent, keeping its world pose; cycles are refused. Undoable.",
          Needs::Document },
        { "transform", "node.transform(id, {position, rotation, scale}) -> {position, rotation, scale}",
          "Sets any of position/rotation/scale (absolute; rotation in euler degrees; omitted parts keep their value) and returns the result. Undoable.",
          Needs::Document },
        { "property", "node.property(id, key) -> value",
          "Reads a reflected property (position, rotation, scale; lights add intensity, lightColor, distance, spotCutOff, spotCutOffSoftness, rectWidth, rectHeight).",
          Needs::Document },
        { "setProperty", "node.setProperty(id, key, value) -> bool",
          "Writes a reflected property (same keys as node.property). Undoable — the write rides the run's undo macro.",
          Needs::Document },
        { "info", "node.info(id) -> {id, name, type, parent, position, rotation, scale}",
          "Everything scene.nodes() reports, for one node.",
          Needs::Document },
        { "boneNames", "node.boneNames(id) -> [string]",
          "The node's rig, in bone-index order (the index its vertex weights name). Empty for anything unrigged.",
          Needs::Document },
        { "skinningMode", "node.skinningMode(id) -> \"gpu\" | \"none\"",
          "How the node deforms: \"gpu\" when it carries a rig (the vertex shader skins position, normal and tangent from bone matrices), \"none\" when it is static. Diagnostic.",
          Needs::Document },
        { "setLightProfile", "node.setLightProfile(id, lightProfileGuid) -> bool",
          "Binds an IES photometric profile (a 'lightprofile' library asset) to a light, shaping its falloff; '' clears it. The asset is pinned into the project as a DEPENDENCY (no companion material is created). Intensity is re-calibrated by the profile's own peak candela scale, so binding one changes the SHAPE of the falloff and not the brightness. RENDERER LIMITS: spot lights always honour a profile; point lights honour it only while they cast NO shadows (a shadow-casting point light is shaded from a code path with no profile term); directional and area lights never do. Undoable (the project pin it creates is not removed again — an unused pin is inert).",
          Needs::Document },
        { "lightProfile", "node.lightProfile(id) -> {guid, path, normalisation, applies}",
          "The light's bound IES profile: the library guid, the resolved file, the photometric scale intensity is divided by, and whether this light type/shadow combination actually honours it. Empty guid = none.",
          Needs::Document },
        { "setLightTexture", "node.setLightTexture(id, textureGuid) -> bool",
          "Binds an image asset as an AREA light's mask/gobo; '' clears it. Pinned as a DEPENDENCY (no companion material). RENDERER LIMIT: only the fast approximation samples the mask — an 'accurate' (LTC) area light ignores it. All masks share one 512x512 sRGB pool (8 distinct images per process); whatever image is bound is resampled into it. Undoable (the project pin it creates is not removed again — an unused pin is inert).",
          Needs::Document },
        { "lightTexture", "node.lightTexture(id) -> {guid, path, applies}",
          "The light's bound area mask: the library guid, the resolved file, and whether this light actually samples it (area + not accurate). Empty guid = none.",
          Needs::Document },
        { "setDecalTexture", "node.setDecalTexture(id, textureGuid) -> bool",
          "Binds an image asset as a DECAL's projected picture; '' clears it (the decal then draws its wire box and projects nothing). Pinned as a BINDING (a dependency row, no companion material). All decal images share one reserved 512x512 sRGB pool, 32 distinct images per process, and whatever image is bound is resampled into it aspect-preserved with transparent padding; the 33rd is REFUSED rather than silently sampling another decal's picture. Undoable (the project pin it creates is not removed again — an unused pin is inert).",
          Needs::Document },
        { "setParticleTexture", "node.setParticleTexture(id, textureGuid) -> bool",
          "Binds a Texture asset as a particle emitter's image, as a BINDING (a dependency row "
          "and a project pin — no copy into the project folder, no companion material). An empty "
          "guid clears it, leaving untextured white quads, which for an additive system means a "
          "solid saturated slab rather than a plume. This is the one emitter field "
          "node.setProperty cannot write: resolving a guid to bytes needs the asset manager, "
          "which the document layer has no access to. Undoable (the project pin it creates is not "
          "removed again — an unused pin is inert).",
          Needs::Document },
        { "particleTexture", "node.particleTexture(id) -> {path}",
          "The emitter's resolved particle image path, or empty.",
          Needs::Document },
        { "decalTexture", "node.decalTexture(id) -> {guid, path}",
          "The decal's bound image: the library guid and the resolved file. Empty guid = none.",
          Needs::Document },
        { "setPlanarReflector", "node.setPlanarReflector(id, enabled) -> bool",
          "Makes this object a planar reflection plane — a mirror or a glossy floor. The plane, its size and its normal are derived from the object's own geometry, so the mesh must be FLAT: its thinnest extent no more than a tenth of the next, i.e. a plane or a thin box. A sphere or a cube is refused with a message. The reflecting face is the object's positive thin axis, so the top of a floor reflects and its underside does not. The object is excluded from its own reflection. Whether a plane actually RENDERS depends on world.setPlanarReflections' budget and on being on screen — each active plane is a whole extra scene render per frame. Undoable.",
          Needs::Document },
        { "planarReflector", "node.planarReflector(id) -> bool",
          "Whether this object is a planar reflection plane.",
          Needs::Document },
        { "physics", "node.physics(id, {type, shape, mass, restitution, friction, damping, collisionMargin}) -> bool",
          "Makes the node a physics body and/or edits its body settings — the Properties panel's "
          "Physics section, as a verb. `type` is \"none\" | \"static\" | \"rigidbody\" (\"none\" "
          "takes the node out of the simulation entirely). `shape` is \"none\" | \"plane\" | "
          "\"sphere\" | \"cube\" | \"convexhull\" | \"trianglemesh\" | \"compound\"; the two that "
          "read geometry (convexhull, trianglemesh) are refused on a node with no mesh. Enum "
          "values travel as NAMES, never ordinals, and an unknown one is refused with the list. "
          "`mass` 0 means an immovable body — setting type \"static\" forces it to 0, and passing "
          "a non-zero mass in the same call is refused rather than silently ignored. "
          "`restitution` is bounciness (0..1), `friction` 0..1, `damping` velocity damping, "
          "`collisionMargin` the collision skin. Values are read at the START of a simulation "
          "(editor.simulate / editor.play), so editing them mid-run does nothing until the next "
          "start. Undoable. NOTE the panel's \"Visible\" checkbox has no key here on purpose: "
          "isVisible is not serialized, so a verb for it would not survive a save.",
          Needs::Document },
        { "physicsInfo", "node.physicsInfo(id) -> {enabled, type, shape, mass, restitution, friction, damping, collisionMargin, isStatic, constraints}",
          "The node's physics body settings, enums as names. `enabled` is the isPhysicsBody flag "
          "(false = the node is not in the simulation and nothing else here is written to the "
          "scene file). `isStatic` is derived, not set directly: it is true for type \"static\" or "
          "mass 0. `constraints` is the COUNT of inter-node constraints on this node — those have "
          "their own UI path and no verb yet (AI_SURFACE_PROGRAM_SPEC owner row D4a), and "
          "node.physics never touches them.",
          Needs::Document },
    };
}

// F5: the asset-binding verbs and setPlanarReflector used to be documented
// "direct document write — not undoable yet" while every skill promised one
// run = one undo step. They stay one service call each; this records that call.
void NodeApi::recordNodeEdit(const QString &text, std::function<void()> redoFn,
                             std::function<void()> undoFn)
{
    if (!host.services || !host.services->undo) return;
    host.services->undo->push(new NodeEditCommand(text, std::move(redoFn), std::move(undoFn)));
}

iris::SceneNodePtr NodeApi::nodeOrFail(const QString &id, const QString &verb)
{
    auto scene = (host.services && host.services->sceneEdit) ? host.services->sceneEdit->scene()
                                                              : iris::ScenePtr();
    if (!scene) {
        fail(QStringLiteral("%1: no scene is open").arg(verb));
        return iris::SceneNodePtr();
    }
    auto node = findNodeByGuid(scene->getRootNode(), id);
    if (!node) fail(QStringLiteral("%1: no node with id '%2'").arg(verb, id));
    return node;
}

bool NodeApi::setPlanarReflector(const QString &id, bool enabled)
{
    auto node = nodeOrFail(id, QStringLiteral("node.setPlanarReflector"));
    if (!node) return false;
    // The Properties row and the "Make Reflective" context action come through
    // the same service call — including the part that reverts the flag when the
    // renderer refuses the geometry, which is the half a second copy forgets.
    QString error;
    IEditorViewport *vp = host.isEngineReady() ? host.viewport : nullptr;
    const bool was = node->getPlanarReflector();
    if (!planarreflectors::set(node, enabled, vp, &error))
        return fail(QStringLiteral("node.setPlanarReflector: %1").arg(error));
    recordNodeEdit(QStringLiteral("planar reflector"),
                   [node, vp, enabled]() { planarreflectors::set(node, enabled, vp, nullptr); },
                   [node, vp, was]() { planarreflectors::set(node, was, vp, nullptr); });
    return true;
}

bool NodeApi::planarReflector(const QString &id)
{
    auto node = nodeOrFail(id, QStringLiteral("node.planarReflector"));
    if (!node) return false;
    return node->getPlanarReflector();
}

bool NodeApi::remove(const QString &id)
{
    auto node = nodeOrFail(id, QStringLiteral("node.remove"));
    if (!node) return false;
    if (!host.services->sceneEdit->deleteNode(node))
        return fail(QStringLiteral("node.remove: '%1' is not removable").arg(node->getName()));
    return true;
}

QString NodeApi::duplicate(const QString &id)
{
    auto node = nodeOrFail(id, QStringLiteral("node.duplicate"));
    if (!node) return QString();
    auto copy = host.services->sceneEdit->duplicateNode(node);
    if (!copy) {
        fail(QStringLiteral("node.duplicate: '%1' is not duplicable").arg(node->getName()));
        return QString();
    }
    return copy->getGUID();
}

bool NodeApi::reparent(const QString &id, const QString &parentId)
{
    auto node = nodeOrFail(id, QStringLiteral("node.reparent"));
    if (!node) return false;
    auto parent = nodeOrFail(parentId, QStringLiteral("node.reparent"));
    if (!parent) return false;
    if (ReparentSceneNodeCommand::wouldCreateCycle(node, parent))
        return fail("node.reparent: that would create a cycle");
    host.services->undo->push(new ReparentSceneNodeCommand(node, parent));
    return true;
}

QVariantMap NodeApi::transform(const QString &id, const QVariantMap &change)
{
    auto node = nodeOrFail(id, QStringLiteral("node.transform"));
    if (!node) return QVariantMap();

    const iris::Vec3 pos = vecFromJs(change.value("position"), node->getLocalPos());
    const iris::Vec3 rotEuler = vecFromJs(change.value("rotation"), node->getLocalRot().toEulerAngles());
    const iris::Vec3 scale = vecFromJs(change.value("scale"), node->getLocalScale());

    host.services->undo->push(new TransformSceneNodeCommand(
        node, pos, iris::Quat::fromEulerAngles(rotEuler), scale));

    return { { "position", vecToJs(node->getLocalPos()) },
             { "rotation", vecToJs(node->getLocalRot().toEulerAngles()) },
             { "scale", vecToJs(node->getLocalScale()) } };
}

QVariant NodeApi::property(const QString &id, const QString &key)
{
    auto node = nodeOrFail(id, QStringLiteral("node.property"));
    if (!node) return QVariant();
    const QVariant value = node->getPropertyValue(key);
    if (!value.isValid()) {
        fail(QStringLiteral("node.property: '%1' has no property '%2'").arg(node->getName(), key));
        return QVariant();
    }
    switch (value.typeId()) {
    case QMetaType::QVector3D: return vecToJs(iris::fromQt(value.value<QVector3D>()));
    case QMetaType::QColor:    return colorToJs(value.value<QColor>());
    default:                   return value;
    }
}

bool NodeApi::setProperty(const QString &id, const QString &key, const QVariant &rawValue)
{
    auto node = nodeOrFail(id, QStringLiteral("node.setProperty"));
    if (!node) return false;
    const QVariant value = normalizeJs(rawValue);

    // The current value tells us the target type — JS maps/strings convert to
    // the vector/colour the document field expects.
    const QVariant current = node->getPropertyValue(key);
    if (!current.isValid())
        return fail(QStringLiteral("node.setProperty: '%1' has no property '%2'").arg(node->getName(), key));

    QVariant converted = value;
    switch (current.typeId()) {
    case QMetaType::QVector3D:
        converted = QVariant::fromValue(iris::toQt(vecFromJs(value, iris::fromQt(current.value<QVector3D>()))));
        break;
    case QMetaType::QColor: {
        // F8: an unparseable colour used to keep the old value and return true.
        bool ok = false;
        const QColor colour = colorFromJs(value, current.value<QColor>(), &ok);
        if (!ok)
            return fail(QStringLiteral("node.setProperty: %1 (property '%2')")
                            .arg(colorHelp(value), key));
        converted = QVariant::fromValue(colour);
        break;
    }
    case QMetaType::Float:
    case QMetaType::Double:
        converted = value.toFloat();
        break;
    default:
        break;
    }

    // The write happens FIRST so a refusal is still reported honestly and never
    // leaves an undo entry behind; the command is then pushed to record it
    // (F5). QUndoStack::push replays redo() immediately — every reflected
    // setter is idempotent (they are plain field assignments), so applying the
    // same value twice is a no-op, and this keeps one code path for "did the
    // node accept it?".
    if (!node->setPropertyValue(key, converted))
        return fail(QStringLiteral("node.setProperty: '%1' rejected property '%2'").arg(node->getName(), key));
    if (host.services && host.services->undo)
        host.services->undo->push(new SetNodePropertyCommand(node, key, current, converted));
    return true;
}

QVariant NodeApi::info(const QString &id)
{
    auto node = nodeOrFail(id, QStringLiteral("node.info"));
    if (!node) return QVariant();
    return nodeToJs(node);
}

// The node's OWN skeleton, not the mesh asset's: the asset carries the rig
// template, shared by every node that references it, and is never posed
// (GPU_SKINNING_SPEC §7).
static iris::SkeletonPtr skeletonOf(const iris::SceneNodePtr &node)
{
    if (!node || node->getSceneNodeType() != iris::SceneNodeType::Mesh) return iris::SkeletonPtr();
    return node.staticCast<iris::MeshNode>()->getSkeleton();
}

QVariant NodeApi::boneNames(const QString &id)
{
    auto node = nodeOrFail(id, QStringLiteral("node.boneNames"));
    if (!node) return QVariant();
    QVariantList out;
    if (auto skel = skeletonOf(node))
        for (const auto &bone : skel->bones) out.append(bone->name);
    return out;
}

QString NodeApi::skinningMode(const QString &id)
{
    auto node = nodeOrFail(id, QStringLiteral("node.skinningMode"));
    if (!node) return QString();
    // "gpu" is the only skinned answer there is: the CPU renderer is gone
    // (GPU_SKINNING_SPEC §6). A rig the engine refuses renders unskinned at bind
    // pose, and the engine says so on its log — the document still has a rig, so
    // this reports what the document asked for.
    return skeletonOf(node).isNull() ? QStringLiteral("none") : QStringLiteral("gpu");
}

// --- Light asset bindings (LIGHTS_COMPLETION_SPEC phases 3 and 5) ----------
// The verbs are the capability; the light property panel calls the SAME
// LightBindings implementation. Resolution, the project dependency pin and the
// photometric re-calibration all live there, never here.

iris::LightNodePtr NodeApi::lightOrFail(const QString &id, const QString &verb)
{
    auto node = nodeOrFail(id, verb);
    if (!node) return iris::LightNodePtr();
    if (node->getSceneNodeType() != iris::SceneNodeType::Light) {
        fail(QStringLiteral("%1: '%2' is not a light").arg(verb, node->getName()));
        return iris::LightNodePtr();
    }
    return node.staticCast<iris::LightNode>();
}

bool NodeApi::setLightProfile(const QString &id, const QString &assetGuid)
{
    auto light = lightOrFail(id, QStringLiteral("node.setLightProfile"));
    if (!light) return false;
    QString error;
    const QString was = light->iesProfileGuid;
    if (!LightBindings::bindProfile(light, assetGuid.trimmed(), host.db, host.project, &error))
        return fail(QStringLiteral("node.setLightProfile: %1").arg(error));
    Database *db = host.db;
    Project *project = host.project;
    const QString now = assetGuid.trimmed();
    recordNodeEdit(QStringLiteral("light profile"),
                   [light, now, db, project]() { LightBindings::bindProfile(light, now, db, project); },
                   [light, was, db, project]() { LightBindings::bindProfile(light, was, db, project); });
    return true;
}

QVariant NodeApi::lightProfile(const QString &id)
{
    auto light = lightOrFail(id, QStringLiteral("node.lightProfile"));
    if (!light) return QVariant();
    // The renderer's own rule, reported rather than hidden: shadow-casting
    // point lights lose their profile, and only spot/point carry one at all.
    const bool shadows = light->shadowMap &&
                         light->shadowMap->shadowType != iris::ShadowMapType::None;
    const bool applies = !light->iesProfileGuid.isEmpty() &&
                         (light->lightType == iris::LightType::Spot ||
                          (light->lightType == iris::LightType::Point && !shadows));
    return QVariantMap{ { "guid", light->iesProfileGuid },
                        { "path", light->iesProfilePath },
                        { "normalisation", light->iesNormalisation },
                        { "applies", applies } };
}

bool NodeApi::setLightTexture(const QString &id, const QString &assetGuid)
{
    auto light = lightOrFail(id, QStringLiteral("node.setLightTexture"));
    if (!light) return false;
    QString error;
    const QString was = light->lightTextureGuid;
    if (!LightBindings::bindTexture(light, assetGuid.trimmed(), host.db, host.project, &error))
        return fail(QStringLiteral("node.setLightTexture: %1").arg(error));
    Database *db = host.db;
    Project *project = host.project;
    const QString now = assetGuid.trimmed();
    recordNodeEdit(QStringLiteral("light mask"),
                   [light, now, db, project]() { LightBindings::bindTexture(light, now, db, project); },
                   [light, was, db, project]() { LightBindings::bindTexture(light, was, db, project); });
    return true;
}

iris::DecalNodePtr NodeApi::decalOrFail(const QString &id, const QString &verb)
{
    auto node = nodeOrFail(id, verb);
    if (!node) return iris::DecalNodePtr();
    if (node->getSceneNodeType() != iris::SceneNodeType::Decal) {
        fail(QStringLiteral("%1: '%2' is not a decal").arg(verb, node->getName()));
        return iris::DecalNodePtr();
    }
    return node.staticCast<iris::DecalNode>();
}

bool NodeApi::setDecalTexture(const QString &id, const QString &assetGuid)
{
    auto decal = decalOrFail(id, QStringLiteral("node.setDecalTexture"));
    if (!decal) return false;
    const QString guid = assetGuid.trimmed();
    if (!guid.isEmpty() && host.db && host.db->fetchAsset(guid).guid.isEmpty())
        return fail(QStringLiteral("node.setDecalTexture: no asset with guid '%1'").arg(guid));
    // THE one binding path — the panel's picker and the asset-bin drop call the
    // same service method (dependency row + AddKind::Binding pin + CAS resolve).
    const QString was = decal->textureGuid;
    if (!host.services || !host.services->sceneEdit ||
        !host.services->sceneEdit->setDecalTexture(decal, guid))
        return fail(QStringLiteral("node.setDecalTexture: could not bind '%1'").arg(guid));
    SceneEditService *edit = host.services->sceneEdit;
    recordNodeEdit(QStringLiteral("decal image"),
                   [edit, decal, guid]() { edit->setDecalTexture(decal, guid); },
                   [edit, decal, was]() { edit->setDecalTexture(decal, was); });
    return true;
}

QVariant NodeApi::decalTexture(const QString &id)
{
    auto decal = decalOrFail(id, QStringLiteral("node.decalTexture"));
    if (!decal) return QVariant();
    return QVariantMap{ { "guid", decal->textureGuid },
                        { "path", decal->resolvedTexturePath } };
}

bool NodeApi::setParticleTexture(const QString &id, const QString &assetGuid)
{
    auto node = nodeOrFail(id, QStringLiteral("node.setParticleTexture"));
    if (!node) return false;
    if (node->getSceneNodeType() != iris::SceneNodeType::ParticleSystem)
        return fail(QStringLiteral("node.setParticleTexture: '%1' is not a particle system")
                        .arg(node->getName()));
    const QString guid = assetGuid.trimmed();
    if (!guid.isEmpty() && host.db && host.db->fetchAsset(guid).guid.isEmpty())
        return fail(QStringLiteral("node.setParticleTexture: no asset with guid '%1'").arg(guid));
    // Same one binding path the decal and light textures use: a dependency row,
    // an AddKind::Binding pin, and a CAS resolve. Never a copy into the project
    // folder, never a companion material.
    auto emitter = node.staticCast<iris::ParticleSystemNode>();
    // The emitter carries no guid field (only the loaded Texture2D), so the
    // previous binding is read back from the dependency row the service writes.
    const QString was = host.db ? host.db->getDependencyByType(
                                      static_cast<int>(ModelTypes::ParticleSystem),
                                      emitter->getGUID())
                                : QString();
    if (!host.services || !host.services->sceneEdit ||
        !host.services->sceneEdit->setParticleTexture(emitter, guid))
        return fail(QStringLiteral("node.setParticleTexture: could not bind '%1'").arg(guid));
    SceneEditService *edit = host.services->sceneEdit;
    recordNodeEdit(QStringLiteral("particle image"),
                   [edit, emitter, guid]() { edit->setParticleTexture(emitter, guid); },
                   [edit, emitter, was]() { edit->setParticleTexture(emitter, was); });
    return true;
}

QVariant NodeApi::particleTexture(const QString &id)
{
    auto node = nodeOrFail(id, QStringLiteral("node.particleTexture"));
    if (!node) return QVariant();
    if (node->getSceneNodeType() != iris::SceneNodeType::ParticleSystem) {
        fail(QStringLiteral("node.particleTexture: '%1' is not a particle system")
                 .arg(node->getName()));
        return QVariant();
    }
    auto ps = node.staticCast<iris::ParticleSystemNode>();
    return QVariantMap{ { "path", ps->texture ? ps->texture->getSource() : QString() } };
}

QVariant NodeApi::lightTexture(const QString &id)
{
    auto light = lightOrFail(id, QStringLiteral("node.lightTexture"));
    if (!light) return QVariant();
    const bool applies = !light->lightTextureGuid.isEmpty() &&
                         light->lightType == iris::LightType::Area && !light->accurate;
    return QVariantMap{ { "guid", light->lightTextureGuid },
                        { "path", light->lightTexturePath },
                        { "applies", applies } };
}

// --- Physics (AI_SURFACE_PROGRAM_SPEC lane D #14) --------------------------
//
// The last UI-with-no-verb domain: iris::PhysicsProperty carries every field,
// SceneWriter serializes them, and the ONLY writer was
// PhysicsPropertyWidget::on*Changed. Two verbs, and the panel stays the
// semantics reference:
//   * enums travel as NAMES (the particles module's rule), never ordinals —
//     an ordinal in a scene file is an implementation detail, and a model that
//     guesses "2" for a shape has no way to be told it guessed wrong;
//   * `isPhysicsBody` is the flag the writer gates on, so type "none" clears
//     it and any other type sets it (exactly what the panel's type combo does);
//   * `isStatic` is derived (type static OR mass 0) rather than exposed —
//     the panel derives it the same way, and two ways to say one thing on an
//     AI surface is a defect waiting to happen.
//
// Scope is owner row D4(a): scalars + shape + type. Constraints are inter-node
// references with their own UI path and are only COUNTED here.
//
// Deliberately absent: `isVisible`. The panel has the checkbox, but
// SceneWriter never writes the field (scenewriter.cpp's physics block), so a
// verb for it would answer true and lose the value on the next open — the
// exact F7/F8 silent-success class this program exists to stop.

namespace {

struct EnumRow { const char *name; int value; };

const EnumRow kPhysicsTypes[] = {
    { "none",      static_cast<int>(iris::PhysicsType::None) },
    { "static",    static_cast<int>(iris::PhysicsType::Static) },
    { "rigidbody", static_cast<int>(iris::PhysicsType::RigidBody) },
    // SoftBody exists in the enum and is deliberately NOT reachable: the panel
    // hides it (physicspropertywidget.cpp) and PhysicsHelper builds no body for
    // it, so accepting the name would be a silent no-op.
};

const EnumRow kPhysicsShapes[] = {
    { "none",         static_cast<int>(iris::PhysicsCollisionShape::None) },
    { "plane",        static_cast<int>(iris::PhysicsCollisionShape::Plane) },
    { "sphere",       static_cast<int>(iris::PhysicsCollisionShape::Sphere) },
    { "cube",         static_cast<int>(iris::PhysicsCollisionShape::Cube) },
    { "convexhull",   static_cast<int>(iris::PhysicsCollisionShape::ConvexHull) },
    { "trianglemesh", static_cast<int>(iris::PhysicsCollisionShape::TriangleMesh) },
    { "compound",     static_cast<int>(iris::PhysicsCollisionShape::Compound) },
};

template <int N>
QString enumName(const EnumRow (&rows)[N], int value)
{
    for (const auto &row : rows)
        if (row.value == value) return QString::fromLatin1(row.name);
    return QStringLiteral("unknown");
}

template <int N>
QStringList enumNames(const EnumRow (&rows)[N])
{
    QStringList out;
    for (const auto &row : rows) out.append(QString::fromLatin1(row.name));
    return out;
}

template <int N>
bool enumValue(const EnumRow (&rows)[N], const QString &name, int *value)
{
    const QString key = name.trimmed().toLower();
    for (const auto &row : rows) {
        if (key == QLatin1String(row.name)) { *value = row.value; return true; }
    }
    return false;
}

QVariantMap physicsToJs(const iris::SceneNodePtr &node)
{
    const auto &p = node->physicsProperty;
    return QVariantMap{
        { "enabled",         node->isPhysicsBody },
        { "type",            enumName(kPhysicsTypes, static_cast<int>(p.type)) },
        { "shape",           enumName(kPhysicsShapes, static_cast<int>(p.shape)) },
        { "mass",            p.objectMass },
        { "restitution",     p.objectRestitution },
        { "friction",        p.objectFriction },
        { "damping",         p.objectDamping },
        { "collisionMargin", p.objectCollisionMargin },
        { "isStatic",        p.isStatic },
        { "constraints",     p.constraints.size() },
    };
}

} // namespace

QVariantMap NodeApi::physicsInfo(const QString &id)
{
    auto node = nodeOrFail(id, QStringLiteral("node.physicsInfo"));
    if (!node) return QVariantMap();
    return physicsToJs(node);
}

bool NodeApi::physics(const QString &id, const QVariantMap &change)
{
    auto node = nodeOrFail(id, QStringLiteral("node.physics"));
    if (!node) return false;

    if (change.isEmpty())
        return fail("node.physics: nothing to change — pass a map "
                    "({type, shape, mass, restitution, friction, damping, collisionMargin}); "
                    "node.physicsInfo(id) reads the current values");

    static const QStringList known = { "type", "shape", "mass", "restitution",
                                       "friction", "damping", "collisionMargin" };
    for (auto it = change.constBegin(); it != change.constEnd(); ++it) {
        if (!known.contains(it.key()))
            return fail(QStringLiteral("node.physics: unknown key '%1' (known: %2). "
                                       "Note isVisible is intentionally absent — it is not "
                                       "serialized, so setting it would be lost on the next open.")
                            .arg(it.key(), known.join(", ")));
    }

    // Everything is validated into a COPY first; the node is only touched once
    // every key is understood, so a refused call changes nothing at all.
    const bool wasBody = node->isPhysicsBody;
    const iris::PhysicsProperty was = node->physicsProperty;
    iris::PhysicsProperty next = was;
    bool nextIsBody = wasBody;

    const auto number = [&](const char *key, float *out) -> bool {
        if (!change.contains(QLatin1String(key))) return true;
        const QVariant raw = scriptmod::normalizeJs(change.value(QLatin1String(key)));
        bool numeric = false;
        const double v = raw.toDouble(&numeric);
        // `true` converts to 1.0 with ok=true — so a booleanised scalar would
        // land as "mass 1" and answer true. That is the silent-success class
        // this program exists to remove; a bool is not a number here.
        if (raw.typeId() == QMetaType::Bool) numeric = false;
        if (!numeric)
            return fail(QStringLiteral("node.physics: '%1' must be a number, got '%2'")
                            .arg(QLatin1String(key), change.value(QLatin1String(key)).toString()));
        *out = float(v);
        return true;
    };

    if (change.contains("type")) {
        int value = 0;
        if (!enumValue(kPhysicsTypes, change.value("type").toString(), &value))
            return fail(QStringLiteral("node.physics: unknown type '%1' (%2)")
                            .arg(change.value("type").toString(),
                                 enumNames(kPhysicsTypes).join(", ")));
        next.type = static_cast<iris::PhysicsType>(value);
        // The panel's type combo does exactly this: None takes the node out of
        // the simulation (isPhysicsBody false, which is what SceneWriter gates
        // the whole physics block on), anything else puts it in.
        nextIsBody = (next.type != iris::PhysicsType::None);
    }

    if (change.contains("shape")) {
        int value = 0;
        if (!enumValue(kPhysicsShapes, change.value("shape").toString(), &value))
            return fail(QStringLiteral("node.physics: unknown shape '%1' (%2)")
                            .arg(change.value("shape").toString(),
                                 enumNames(kPhysicsShapes).join(", ")));
        next.shape = static_cast<iris::PhysicsCollisionShape>(value);
        // PhysicsHelper's ConvexHull/TriangleMesh branches call
        // meshNode->getMesh() through an unchecked staticCast — on a node with
        // no mesh that is a read past the object. The panel hides these rows
        // for Empty nodes; a verb has to say no out loud instead.
        const bool needsGeometry = next.shape == iris::PhysicsCollisionShape::ConvexHull ||
                                   next.shape == iris::PhysicsCollisionShape::TriangleMesh;
        if (needsGeometry && node->getSceneNodeType() != iris::SceneNodeType::Mesh)
            return fail(QStringLiteral("node.physics: shape '%1' is built from the node's geometry "
                                       "and '%2' is a %3, not a mesh — use sphere, cube, plane or "
                                       "compound")
                            .arg(change.value("shape").toString().trimmed().toLower(),
                                 node->getName(), nodeTypeName(node->getSceneNodeType())));
    }

    if (!number("mass", &next.objectMass)) return false;
    if (!number("restitution", &next.objectRestitution)) return false;
    if (!number("friction", &next.objectFriction)) return false;
    if (!number("damping", &next.objectDamping)) return false;
    if (!number("collisionMargin", &next.objectCollisionMargin)) return false;

    if (next.objectMass < 0.0f)
        return fail(QStringLiteral("node.physics: mass must be >= 0 (0 = immovable), got %1")
                        .arg(double(next.objectMass)));

    // A static body has mass 0 in Bullet — the panel's type combo zeroes the
    // mass slider for exactly this reason. Doing it silently when the caller
    // ALSO asked for a mass would be the quiet-lie class, so that combination
    // is refused and the plain "type: static" case is documented.
    if (next.type == iris::PhysicsType::Static) {
        if (change.contains("mass") && next.objectMass != 0.0f)
            return fail(QStringLiteral("node.physics: a \"static\" body has mass 0 — "
                                       "drop the mass, or use type \"rigidbody\" for mass %1")
                            .arg(double(next.objectMass)));
        next.objectMass = 0.0f;
    }

    // Derived, exactly as the panel derives it. Not a key of its own.
    next.isStatic = (next.type == iris::PhysicsType::Static) || next.objectMass == 0.0f;

    auto apply = [](const iris::SceneNodePtr &n, const iris::PhysicsProperty &props, bool isBody) {
        // Field-wise, never a whole-struct assign: constraints, centerOfMass
        // and pivotPoint are outside this verb's scope and a struct copy would
        // silently carry (or drop) them.
        n->physicsProperty.type = props.type;
        n->physicsProperty.shape = props.shape;
        n->physicsProperty.objectMass = props.objectMass;
        n->physicsProperty.objectRestitution = props.objectRestitution;
        n->physicsProperty.objectFriction = props.objectFriction;
        n->physicsProperty.objectDamping = props.objectDamping;
        n->physicsProperty.objectCollisionMargin = props.objectCollisionMargin;
        n->physicsProperty.isStatic = props.isStatic;
        n->isPhysicsBody = isBody;
    };

    apply(node, next, nextIsBody);
    recordNodeEdit(QStringLiteral("physics"),
                   [node, next, nextIsBody, apply]() { apply(node, next, nextIsBody); },
                   [node, was, wasBody, apply]() { apply(node, was, wasBody); });
    return true;
}
