/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "scripting/modules/sceneapi.h"


#include <QtMath>

#include "scripting/modules/moduleshared.h"
#include "irisgl/document/scenegraph/scenepicking.h"
#include <algorithm>
#include <cmath>
#include "scripting/modules/cameraapi.h"   // camerashared::applySettings / settingsToJs
#include "commands/reparentscenenodecommand.h"
#include "commands/transformscenenodecommand.h"
#include "shell/mainwindow.h"
#include "services/sceneeditservice.h"
#include "services/selectionservice.h"
#include "services/services.h"
#include "services/undoservice.h"
#include "data/database/database.h"
#include "irisgl/document/scenegraph/particlesystemnode.h"
#include "irisgl/document/scenegraph/cameranode.h"

using namespace scriptmod;

namespace {
const QStringList kPrimitives = {
    "Plane", "Ground", "Cone", "Cube", "Cylinder", "Sphere", "Torus",
    "Capsule", "Gear", "Pyramid", "Teapot", "Sponge", "Steps"
};
}

QVector<VerbInfo> SceneApi::verbs() const
{
    return {
        { "nodes", "scene.nodes({subtree?, depth?, include?}) -> [{id, name, type, parent, position, rotation, scale, …}]",
          "Every node in the open scene, depth-first from the root. With no argument that is the WHOLE tree, which on a "
          "large scene is the most expensive read on this surface — the options bound it. `subtree` is a node id to start "
          "from (that node and its descendants, itself included). `depth` is how many levels BELOW the start to walk: 0 is "
          "the start node alone, 1 adds its children, and omitting it (or a negative value) means the whole subtree; a row "
          "whose children were cut off carries `childCount` and `truncated: true`, so nothing is silently missing. "
          "`include` is an array of extra blocks per row: \"materials\" attaches a `material` summary to mesh nodes "
          "(class, baseColor, metallic, roughness, emissive, alpha mode, and which texture slots are in use — "
          "material.get(id) is still the full read), \"lights\" attaches a `light` block to light nodes (only the rows that "
          "mean something for that light type), and \"visibility\" attaches `visible` plus `visibleInScene`, which is false "
          "as soon as any ANCESTOR is hidden. Unknown option keys and unknown include names are refused, not ignored.",
          Needs::Document },
        { "find", "scene.find(name) -> id | null",
          "The first node with this exact name, or null.",
          Needs::Document },
        { "root", "scene.root() -> id",
          "The scene root's id.",
          Needs::Document },
        { "raycast", "scene.raycast(origin, direction, {maxDistance, includeUnpickable}) -> [{id, name, point, distance, triangleIndex}]",
          "Casts a ray through the document's meshes and returns every triangle-level hit, "
          "nearest first. `origin` and `direction` are {x,y,z} (direction need not be "
          "normalized); `maxDistance` defaults to 10000. Honors each node's `pickable` flag "
          "unless `includeUnpickable` is true, and never returns hidden nodes — the same "
          "semantics the viewport's click uses. This is the API's click: pair it with "
          "editor.select(id) to select what a user would have clicked.",
          Needs::Document },
        { "addPrimitive", "scene.addPrimitive(name, {position, rotation, scale, parent, count}) -> id | [id]",
          "Adds a built-in primitive: plane, ground, cone, cube, cylinder, sphere, torus, capsule, "
          "gear, pyramid, teapot, sponge, steps ('ground' is the large floor plane the Add menu "
          "offers). {count: N} adds N of them and returns an ARRAY of ids instead of one id; "
          "every copy gets the same position/rotation/scale/parent options, so move them "
          "afterwards with node.transform. Undoable — the whole batch is one step of the run's "
          "undo macro.",
          Needs::Document },
        { "addLight", "scene.addLight(type, {position, ...}) -> id",
          "Adds a light: point, spot, directional or area. Undoable.",
          Needs::Document },
        { "addEmpty", "scene.addEmpty({position, parent}) -> id",
          "Adds an empty group node. Undoable.",
          Needs::Document },
        { "addViewer", "scene.addViewer({position, rotation, scale, parent}) -> id",
          "Adds a viewer node — the hierarchy panel's \"Viewer\" action, named \"Avatar\". A viewer "
          "is the scene's first-person stand-in: SIDE EFFECT, and it is not optional — the new "
          "viewer TAKES the active character controller (every other viewer in the scene is "
          "deactivated) and registers itself with the physics world, so a second addViewer "
          "silently demotes the first. Without {position} the node spawns in front of the editor "
          "camera like every other add. This verb only creates the node; nothing walks or drives "
          "it until play mode builds its controller. Undoable.",
          Needs::Document },
        { "addMesh", "scene.addMesh(path, {position, ...}) -> REFUSED",
          "REMOVED — this verb always fails. It used to parse a mesh file straight into the "
          "scene, which wrote the DISK PATH where the reader expects an asset guid: the node "
          "came back empty on the next open and never exported (the archiver is asset-row "
          "driven). Use assets.importAndPlace(path, {position}) instead — it runs the ONE import "
          "pipeline and returns {assetGuid, projectGuid, nodeId}. (It is the three calls "
          "assets.importFile / addToProject / addToScene in one; only the last of those is "
          "undoable.)",
          Needs::Document },
        { "addImagePlane", "scene.addImagePlane(textureGuid, {position?, doubleSided?}) -> id",
          "Spawns an image plane for a Texture asset (IMAGE_PLANE_SPEC option A): a plane sized to the image's aspect (long side 1 m), facing the editor camera at creation, with a basic PBR material carrying the image as baseColorMap (roughness 1, metallic 0; images with an alpha channel blend). Bytes resolve pin-first through the CAS. doubleSided defaults true. Undoable.",
          Needs::Document },
        { "addDecal", "scene.addDecal(textureGuid, {position?, rotation?, scale?, parent?, width?, height?, depth?, metalness?, roughness?, ignoreAlphaDiffuse?}) -> id",
          "Adds a projected-texture decal (DECALS_SPEC) bound to a Texture asset: an oriented box that "
          "paints the image onto every surface inside it, projecting down the node's -Y (the light "
          "convention) and masked by the image's alpha. width is the local-X extent, height the local-Z "
          "extent (the image's V axis) and depth the projection thickness; the node's own scale "
          "multiplies all three. textureGuid may be empty — the decal then draws its wire box and "
          "projects nothing until an image is bound. The image is pinned into the project as a "
          "BINDING (a dependency row, no companion PBR material is minted). There is deliberately no per-decal opacity "
          "or colour tint: the renderer packs four floats per decal and neither fits. Undoable.",
          Needs::Document },
        { "addParticles", "scene.addParticles(preset?, {position?, rotation?, scale?, parent?, rate?, quota?}) -> id",
          "Adds a particle emitter, optionally stamped from a recipe (particles.presets() lists them; "
          "\"fire\" is the one the sample scene uses). The ENGINE simulates it — the document holds "
          "only authoring parameters — so it starts emitting on the next rendered frame with no tick "
          "from anyone. Scalar rows afterwards go through node.setProperty; the over-life ramps "
          "through particles.setColourKeys / setScaleKeys. NOTE the node's scale does not resize the "
          "spawn volume or the particles: both are numeric (extents, particleScale). Undoable.",
          Needs::Document },
        { "addCamera", "scene.addCamera({position?, rotation?, scale?, parent?, settings?}) -> id",
          "Adds a real scene CAMERA (CAMERAS_SPEC): a node that saves, duplicates, deletes, "
          "parents and animates like any other, with a lens (vertical FOV or focal length "
          "through a 36x24 mm sensor), clip planes, aspect + output height, and the focus/DOF "
          "block. It is NOT the viewport's explorer camera, which is not a scene node at all. "
          "`settings` is a camera.settings block applied to the new camera in the same undo "
          "step. Adding a camera does NOT arm it for play — that is scene.setActiveCamera, "
          "always an explicit choice. Undoable.",
          Needs::Document },
        { "cameras", "scene.cameras() -> [{id, name, active, position, rotation, angle, focalLength, ...}]",
          "Every scene camera, with its full settings block and an `active` flag marking the one "
          "play renders through. The viewport's explorer camera is not in this list (it is not a "
          "scene node); editor.camera() reads that one.",
          Needs::Document },
        { "setActiveCamera", "scene.setActiveCamera(id | null) -> bool",
          "Points PLAY at a scene camera: editor Play and the player view render through it "
          "instead of the free viewer. null (or no argument) clears it. EDITING is unaffected — "
          "the main viewport stays the explorer. Switching this from a script or an animation "
          "while playing is a camera cut. Saved with the scene; a camera that is deleted clears "
          "it rather than leaving play pointed at nothing.",
          Needs::Document },
        { "activeCamera", "scene.activeCamera() -> id | null",
          "The camera play renders through, or null for the free viewer.",
          Needs::Document },
    };
}

iris::ScenePtr SceneApi::sceneOrFail()
{
    auto scene = (host.services && host.services->sceneEdit) ? host.services->sceneEdit->scene()
                                                              : iris::ScenePtr();
    if (!scene) fail("scene: no scene is open — project.open() or project.create() first");
    return scene;
}

QVariantList SceneApi::nodes(const QVariant &options)
{
    QVariantList out;
    auto scene = sceneOrFail();
    if (!scene) return out;

    const QVariant normalized = scriptmod::normalizeJs(options);
    QVariantMap params;
    if (normalized.isValid() && !normalized.isNull()) {
        if (normalized.typeId() != QMetaType::QVariantMap) {
            fail("scene.nodes: the argument is an object — {subtree?, depth?, include?}");
            return out;
        }
        params = normalized.toMap();
    }
    static const QStringList knownKeys{ QStringLiteral("subtree"), QStringLiteral("depth"),
                                        QStringLiteral("include") };
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        if (!knownKeys.contains(it.key())) {
            fail(QStringLiteral("scene.nodes: unknown option '%1' (known: %2)")
                     .arg(it.key(), knownKeys.join(QStringLiteral(", "))));
            return out;
        }
    }

    iris::SceneNodePtr start = scene->getRootNode();
    if (params.contains(QStringLiteral("subtree"))) {
        const QString id = params.value(QStringLiteral("subtree")).toString();
        start = findNodeByGuid(scene->getRootNode(), id);
        if (!start) {
            fail(QStringLiteral("scene.nodes: no node with id '%1' to start the subtree from").arg(id));
            return out;
        }
    }

    int depth = -1;   // -1 = unbounded, the historic behaviour
    if (params.contains(QStringLiteral("depth"))) {
        bool numeric = false;
        const int d = params.value(QStringLiteral("depth")).toInt(&numeric);
        if (!numeric) { fail("scene.nodes: depth must be a number"); return out; }
        depth = d < 0 ? -1 : d;
    }

    bool wantMaterials = false, wantLights = false, wantVisibility = false;
    if (params.contains(QStringLiteral("include"))) {
        const QVariant raw = scriptmod::normalizeJs(params.value(QStringLiteral("include")));
        QVariantList names = raw.typeId() == QMetaType::QVariantList ? raw.toList()
                                                                    : QVariantList{ raw };
        for (const QVariant &n : names) {
            const QString name = n.toString();
            if (name == QLatin1String("materials")) wantMaterials = true;
            else if (name == QLatin1String("lights")) wantLights = true;
            else if (name == QLatin1String("visibility")) wantVisibility = true;
            else {
                fail(QStringLiteral("scene.nodes: unknown include '%1' "
                                    "(known: materials, lights, visibility)").arg(name));
                return out;
            }
        }
    }

    std::function<void(const iris::SceneNodePtr &, int)> walk =
        [&](const iris::SceneNodePtr &node, int level) {
        QVariantMap row = nodeToJs(node);
        if (wantMaterials) {
            const QVariantMap material = scriptmod::materialSummaryToJs(node);
            if (!material.isEmpty()) row["material"] = material;
        }
        if (wantLights) {
            const QVariantMap light = scriptmod::lightToJs(node);
            if (!light.isEmpty()) row["light"] = light;
        }
        if (wantVisibility) {
            const QVariantMap vis = scriptmod::visibilityToJs(node);
            for (auto it = vis.constBegin(); it != vis.constEnd(); ++it) row[it.key()] = it.value();
        }
        // A subtree that gets cut off says so, with its size — the whole point
        // of a bounded read is that the caller knows what it did not get.
        // childCount()/childAt(), not children(): the old shape called
        // children() up to THREE times per node — each one a QList and a
        // refcount per child — to answer two questions and then walk.
        const int kids = node->childCount();
        const bool cutOff = (depth >= 0 && level >= depth && kids > 0);
        if (cutOff) {
            row["childCount"] = kids;
            row["truncated"] = true;
        }
        out.append(row);
        if (cutOff) return;
        for (int i = 0; i < kids; ++i)
            if (iris::SceneNode *c = node->childAt(i)) walk(c->sharedFromThis(), level + 1);
    };
    walk(start, 0);
    return out;
}

QVariant SceneApi::find(const QString &name)
{
    auto scene = sceneOrFail();
    if (!scene) return QVariant();
    std::function<iris::SceneNodePtr(const iris::SceneNodePtr &)> walk =
        [&](const iris::SceneNodePtr &node) -> iris::SceneNodePtr {
        if (node->getName() == name) return node;
        const int kids = node->childCount();
        for (int i = 0; i < kids; ++i)
            if (iris::SceneNode *c = node->childAt(i))
                if (auto hit = walk(c->sharedFromThis())) return hit;
        return iris::SceneNodePtr();
    };
    auto hit = walk(scene->getRootNode());
    return hit ? QVariant(hit->getGUID()) : QVariant();
}

QString SceneApi::root()
{
    auto scene = sceneOrFail();
    return scene ? scene->getRootNode()->getGUID() : QString();
}

QVariantList SceneApi::raycast(const QVariant &origin, const QVariant &direction,
                               const QVariantMap &options)
{
    QVariantList out;
    auto scene = sceneOrFail();
    if (!scene) return out;
    if (!origin.isValid() || !direction.isValid()) {
        fail(QStringLiteral("scene.raycast: origin and direction are required ({x,y,z})"));
        return out;
    }
    const iris::Vec3 a = vecFromJs(origin);
    iris::Vec3 dir = vecFromJs(direction);
    if (dir.lengthSquared() <= 0.0f) {
        fail(QStringLiteral("scene.raycast: direction must be non-zero"));
        return out;
    }
    dir.normalize();
    const float maxDistance =
        options.value(QStringLiteral("maxDistance"), 10000.0f).toFloat();
    const bool includeUnpickable =
        options.value(QStringLiteral("includeUnpickable"), false).toBool();

    auto hits = iris::picking::raycastMeshes(scene.data(), a, a + dir * maxDistance,
                                             0, includeUnpickable);
    std::sort(hits.begin(), hits.end(), [](const iris::MeshPick &l,
                                           const iris::MeshPick &r) {
        return l.distanceSqrd < r.distanceSqrd;
    });
    out.reserve(hits.size());
    for (const auto &h : hits) {
        if (!h.node) continue;
        QVariantMap m;
        m.insert(QStringLiteral("id"), h.node->getGUID());
        m.insert(QStringLiteral("name"), h.node->getName());
        QVariantMap p;
        p.insert(QStringLiteral("x"), h.hitPoint.x());
        p.insert(QStringLiteral("y"), h.hitPoint.y());
        p.insert(QStringLiteral("z"), h.hitPoint.z());
        m.insert(QStringLiteral("point"), p);
        m.insert(QStringLiteral("distance"), std::sqrt(h.distanceSqrd));
        m.insert(QStringLiteral("triangleIndex"), h.triangleIndex);
        out.append(m);
    }
    return out;
}

bool SceneApi::applyOptions(const iris::SceneNodePtr &node, const QVariantMap &options, const QString &verb)
{
    if (options.contains("parent")) {
        auto scene = host.services->sceneEdit->scene();
        auto parent = findNodeByGuid(scene->getRootNode(), options.value("parent").toString());
        if (!parent)
            return fail(QStringLiteral("%1: no node with id '%2' for parent").arg(verb, options.value("parent").toString()));
        if (ReparentSceneNodeCommand::wouldCreateCycle(node, parent))
            return fail(QStringLiteral("%1: reparenting would create a cycle").arg(verb));
        host.services->undo->push(new ReparentSceneNodeCommand(node, parent));
    }
    if (options.contains("position") || options.contains("rotation") || options.contains("scale")) {
        const iris::Vec3 pos = vecFromJs(options.value("position"), node->getLocalPos());
        const iris::Vec3 rotEuler = vecFromJs(options.value("rotation"), node->getLocalRot().toEulerAngles());
        const iris::Vec3 scale = vecFromJs(options.value("scale"), node->getLocalScale());
        host.services->undo->push(new TransformSceneNodeCommand(
            node, pos, iris::Quat::fromEulerAngles(rotEuler), scale));
    }
    return true;
}

QString SceneApi::finishAdd(const QVariantMap &options, const QString &verb)
{
    // The add-verb funnel selects the new node (AddSceneNodeCommand::redo);
    // callers deselected beforehand so a silent failure reads as "no node".
    auto node = host.services->selection->selected();
    if (!node) {
        fail(QStringLiteral("%1: the node was not created").arg(verb));
        return QString();
    }
    if (!applyOptions(node, options, verb)) return QString();
    // applyOptions' transform/reparent writes fire rule 4 (a transform write
    // demotes) on a node that was BORN this call — "create at this position"
    // is placement, not a move, so the static default is re-derived. Without
    // this, every scripted/MCP-built scene is fully dynamic while the same
    // adds through the UI keep SCENE_STATIC (scripting audit F1).
    if (!options.isEmpty()) node->applyStaticDefaults();
    return node->getGUID();
}

QVariant SceneApi::addPrimitive(const QString &name, const QVariantMap &options)
{
    if (!requireProject()) return QVariant();   // the primitive gets a DB asset row
    if (!sceneOrFail()) return QVariant();

    QString normalized = name.trimmed().toLower();
    if (!normalized.isEmpty()) normalized[0] = normalized[0].toUpper();
    if (!kPrimitives.contains(normalized)) {
        fail(QStringLiteral("scene.addPrimitive: unknown primitive '%1' (try: %2)")
                 .arg(name, kPrimitives.join(", ").toLower()));
        return QVariant();
    }

    // {count} is the batch form (AI_SURFACE_AUDIT #16). One id for the default
    // and for count 1, an array from 2 up — a caller that never passes count
    // sees exactly what it saw before. Everything still rides the run's single
    // undo macro, so one Ctrl+Z removes the whole batch.
    const QVariant countValue = normalizeJs(options.value(QStringLiteral("count")));
    int count = 1;
    if (countValue.isValid() && !countValue.isNull()) {
        bool numeric = false;
        const double asked = countValue.toDouble(&numeric);
        if (!numeric || asked != qFloor(asked) || asked < 1) {
            fail(QStringLiteral("scene.addPrimitive: count must be a whole number >= 1, got '%1'")
                     .arg(countValue.toString()));
            return QVariant();
        }
        // A bound, because the verb is reachable from a model: 256 primitives
        // is already an absurd single call, and an unbounded one is a way to
        // wedge the editor by typo.
        if (asked > 256) {
            fail(QStringLiteral("scene.addPrimitive: count %1 is above the 256 limit for one call")
                     .arg(qint64(asked)));
            return QVariant();
        }
        count = int(asked);
    }

    QVariantList ids;
    for (int i = 0; i < count; ++i) {
        host.services->selection->select(iris::SceneNodePtr());
        host.services->sceneEdit->addPrimitive(normalized);
        const QString id = finishAdd(options, QStringLiteral("scene.addPrimitive"));
        if (id.isEmpty()) return QVariant();   // finishAdd already threw
        ids.append(id);
    }
    return count > 1 ? QVariant(ids) : QVariant(ids.first().toString());
}

QString SceneApi::addLight(const QString &type, const QVariantMap &options)
{
    if (!sceneOrFail()) return QString();
    const QString t = type.trimmed().toLower();

    host.services->selection->select(iris::SceneNodePtr());
    if (t == "point")            host.services->sceneEdit->addPointLight();
    else if (t == "spot")        host.services->sceneEdit->addSpotLight();
    else if (t == "directional") host.services->sceneEdit->addDirectionalLight();
    else if (t == "area")        host.services->sceneEdit->addAreaLight();
    else {
        fail(QStringLiteral("scene.addLight: unknown type '%1' (point, spot, directional, area)").arg(type));
        return QString();
    }
    return finishAdd(options, QStringLiteral("scene.addLight"));
}

QString SceneApi::addEmpty(const QVariantMap &options)
{
    if (!sceneOrFail()) return QString();
    host.services->selection->select(iris::SceneNodePtr());
    host.services->sceneEdit->addEmpty();
    return finishAdd(options, QStringLiteral("scene.addEmpty"));
}

// AI_SURFACE_PROGRAM_SPEC lane D #12. The service used to return void, which
// is the whole reason this verb could not exist; it now hands the node back so
// a scene-less call fails loudly instead of reporting whatever happened to be
// selected. `ignorePlacement` keeps the node at the origin when the caller
// gave a position — applyOptions then puts it exactly there, in one transform
// command, rather than moving it twice.
QString SceneApi::addViewer(const QVariantMap &options)
{
    if (!sceneOrFail()) return QString();
    host.services->selection->select(iris::SceneNodePtr());
    auto node = host.services->sceneEdit->addViewer(options.contains("position"));
    if (!node) {
        fail(QStringLiteral("scene.addViewer: the viewer was not created"));
        return QString();
    }
    return finishAdd(options, QStringLiteral("scene.addViewer"));
}

QString SceneApi::addCamera(const QVariantMap &options)
{
    if (!sceneOrFail()) return QString();

    // `settings` is not a transform option — pull it out before finishAdd sees
    // the map, and apply it through the camera module so there is exactly ONE
    // implementation of "write a camera's settings" (and one set of refusals).
    QVariantMap rest = options;
    const QVariant settings = normalizeJs(rest.take(QStringLiteral("settings")));

    host.services->selection->select(iris::SceneNodePtr());
    auto node = host.services->sceneEdit->addCamera(rest.contains("position"));
    if (!node) {
        fail(QStringLiteral("scene.addCamera: the camera was not created"));
        return QString();
    }
    const QString id = finishAdd(rest, QStringLiteral("scene.addCamera"));
    if (id.isEmpty()) return QString();   // finishAdd already threw

    if (settings.isValid() && !settings.isNull()) {
        if (settings.typeId() != QMetaType::QVariantMap) {
            fail("scene.addCamera: `settings` is an object — the same block camera.settings takes");
            return QString();
        }
        // THE SAME writer camera.settings uses, refusing the same things in the
        // same words. The node stays: the add is already on the undo stack and
        // one Ctrl+Z (or the run's macro) takes the whole thing back — leaving a
        // half-configured camera behind is better than a silently missing one.
        const QString error = camerashared::applySettings(
            node.staticCast<iris::CameraNode>(), settings.toMap(),
            host.services->sceneEdit->scene(), host.services->undo,
            QStringLiteral("scene.addCamera"));
        if (!error.isEmpty()) { fail(error); return QString(); }
    }
    return id;
}

QVariantList SceneApi::cameras()
{
    QVariantList out;
    auto scene = sceneOrFail();
    if (!scene) return out;

    // Walked from the root rather than read out of Scene::cameras, so the rows
    // come back in scene-graph order (stable, and what the hierarchy shows)
    // instead of a QHash's.
    std::function<void(const iris::SceneNodePtr &)> walk = [&](const iris::SceneNodePtr &node) {
        if (node->getSceneNodeType() == iris::SceneNodeType::Camera) {
            QVariantMap row = nodeToJs(node);
            const QVariantMap block =
                camerashared::settingsToJs(node.staticCast<iris::CameraNode>());
            for (auto it = block.constBegin(); it != block.constEnd(); ++it)
                if (!row.contains(it.key())) row[it.key()] = it.value();
            row["active"] = (scene->getActiveCameraGuid() == node->getGUID());
            out.append(row);
        }
        const int kids = node->childCount();
        for (int i = 0; i < kids; ++i)
            if (iris::SceneNode *c = node->childAt(i)) walk(c->sharedFromThis());
    };
    walk(scene->getRootNode());
    return out;
}

bool SceneApi::setActiveCamera(const QVariant &id)
{
    auto scene = sceneOrFail();
    if (!scene) return false;

    const QVariant value = normalizeJs(id);
    // No argument, null and "" all mean the same thing: back to the free viewer.
    const QString guid = (!value.isValid() || value.isNull()) ? QString() : value.toString();
    if (!scene->setActiveCamera(guid))
        return fail(QStringLiteral("scene.setActiveCamera: '%1' is not a camera in this scene "
                                   "(scene.cameras() lists them; pass null for the free viewer)")
                        .arg(guid));
    return true;
}

QVariant SceneApi::activeCamera()
{
    auto scene = sceneOrFail();
    if (!scene) return QVariant();
    const QString guid = scene->getActiveCameraGuid();
    return guid.isEmpty() ? QVariant() : QVariant(guid);
}

QString SceneApi::addImagePlane(const QString &textureGuid, const QVariantMap &options)
{
    if (!requireProject()) return QString();   // the plane gets a DB object row + dependency
    if (!sceneOrFail()) return QString();

    ImagePlaneOptions opts;
    opts.doubleSided = options.value("doubleSided", true).toBool();

    host.services->selection->select(iris::SceneNodePtr());
    auto node = host.services->sceneEdit->addImagePlane(
        textureGuid, vecFromJs(options.value("position")), opts);
    if (!node) {
        fail(QStringLiteral("scene.addImagePlane: '%1' is not a resolvable image texture asset")
                 .arg(textureGuid));
        return QString();
    }
    // position is consumed by the service (the drop point); strip it so
    // applyOptions does not re-apply it as a second transform command.
    QVariantMap rest = options;
    rest.remove("position");
    rest.remove("doubleSided");
    return finishAdd(rest, QStringLiteral("scene.addImagePlane"));
}

QString SceneApi::addDecal(const QString &textureGuid, const QVariantMap &options)
{
    if (!requireProject()) return QString();   // the decal gets a DB object row + dependency
    if (!sceneOrFail()) return QString();

    if (!textureGuid.isEmpty() && host.db &&
        host.db->fetchAsset(textureGuid).guid.isEmpty()) {
        fail(QStringLiteral("scene.addDecal: no asset with guid '%1'").arg(textureGuid));
        return QString();
    }

    DecalOptions opts;
    opts.width  = options.value("width",  double(opts.width)).toFloat();
    opts.height = options.value("height", double(opts.height)).toFloat();
    opts.depth  = options.value("depth",  double(opts.depth)).toFloat();
    opts.metalness = options.value("metalness", double(opts.metalness)).toFloat();
    opts.roughness = options.value("roughness", double(opts.roughness)).toFloat();
    opts.ignoreAlphaDiffuse = options.value("ignoreAlphaDiffuse", false).toBool();
    if (options.contains("position")) {
        opts.positionGiven = true;
        opts.position = vecFromJs(options.value("position"));
    }

    host.services->selection->select(iris::SceneNodePtr());
    auto node = host.services->sceneEdit->addDecal(textureGuid, opts);
    if (!node) {
        fail(QStringLiteral("scene.addDecal: no scene is open"));
        return QString();
    }
    // The service consumed the box/material options and (when given) the
    // position; strip them so applyOptions does not re-apply a second transform.
    QVariantMap rest = options;
    for (const char *k : { "position", "width", "height", "depth",
                           "metalness", "roughness", "ignoreAlphaDiffuse" })
        rest.remove(QString::fromLatin1(k));
    return finishAdd(rest, QStringLiteral("scene.addDecal"));
}

QString SceneApi::addParticles(const QString &preset, const QVariantMap &options)
{
    if (!requireProject()) return QString();   // the emitter gets a DB object row + a texture
    if (!sceneOrFail()) return QString();

    iris::ParticlePreset recipe = iris::ParticlePreset::Custom;
    if (!preset.isEmpty()) {
        const QStringList known = iris::ParticleSystemNode::presetNames();
        const bool ok = std::any_of(known.begin(), known.end(), [&](const QString &k) {
            return k.compare(preset, Qt::CaseInsensitive) == 0;
        });
        if (!ok) {
            fail(QStringLiteral("scene.addParticles: unknown preset '%1' (try: %2)")
                     .arg(preset, known.join(", ")));
            return QString();
        }
        recipe = iris::ParticleSystemNode::presetFromName(preset);
    }

    host.services->selection->select(iris::SceneNodePtr());
    auto node = host.services->sceneEdit->addParticleSystem(recipe);
    if (!node) {
        fail(QStringLiteral("scene.addParticles: the emitter was not created"));
        return QString();
    }
    // Two convenience rows, applied AFTER the recipe so an explicit value wins:
    // everything else goes through node.setProperty, which reaches every
    // reflected field on the node and needs no duplication here.
    if (options.contains("rate"))  node->particlesPerSecond = options.value("rate").toFloat();
    if (options.contains("quota")) node->maxParticles = options.value("quota").toInt();
    QVariantMap rest = options;
    rest.remove("rate");
    rest.remove("quota");
    return finishAdd(rest, QStringLiteral("scene.addParticles"));
}

// F1 (AI_SURFACE_AUDIT, CRITICAL): this verb was a DATA-LOSS verb and now
// refuses instead of pretending.
//
// It parsed the file with iris::MeshNode::loadAsSceneFragment and dropped the
// resulting node into the scene with no library asset behind it. SceneWriter
// then wrote the node's disk path into "mesh" (scenewriter.cpp) where
// SceneReader resolves that field AS A GUID (scenereader.cpp) — so the node
// reloaded with no geometry, and it could never export because the archiver
// walks asset rows. Nothing in the UI used this path either (the drag-drop and
// menu import routes both go through the asset pipeline); the verb was its only
// live caller, and the shipped assets skill taught it as a shortcut (F2).
//
// Failing loudly rather than silently re-routing: the import half of the correct
// flow is NOT undoable (assets.importFile), so quietly turning a verb documented
// "Undoable" into a half-undoable one would just trade this defect for the F5
// class. ASSET_PIPELINE_SPEC's "one pipeline" rule says the composition belongs
// in assets.importAndPlace (AI_SURFACE_PROGRAM_SPEC lane D #7) — which now
// exists, so the message points at it first and keeps the three explicit calls
// underneath (a reader who wants the drawer/pin steps apart still needs them).
QString SceneApi::addMesh(const QString &path, const QVariantMap &options)
{
    Q_UNUSED(options);
    fail(QStringLiteral(
             "scene.addMesh is removed: it wrote the disk path '%1' where the scene reader "
             "expects an asset guid, so the node came back EMPTY on the next open and never "
             "exported. Import it properly instead:\n"
             "  assets.importAndPlace(\"%1\", {position: ...});  // -> {assetGuid, projectGuid, nodeId}\n"
             "which is these three, in one call (only the last is undoable):\n"
             "  var g = assets.importFile(\"%1\");   // library asset (not undoable)\n"
             "  var p = assets.addToProject(g);      // pin it into this project (not undoable)\n"
             "  assets.addToScene(p, {position: ...}); // places it (undoable)")
             .arg(path));
    return QString();
}
