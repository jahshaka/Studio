/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/scenetemplatebuilder.h"

#include <QColor>
#include <QJsonDocument>
#include <QJsonObject>

#include "data/constants.h"
#include "data/database/database.h"
#include "data/guidmanager.h"
#include "data/project.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/document/assets/texture2d.h"
#include "irisgl/document/physics/physicsproperties.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "services/defaultfloormaterial.h"
#include "services/scenenodehelper.h"
#include "services/worldmodes.h"

namespace scenetemplate {

iris::SceneNodePtr buildFloor(SceneTemplate kind, Database *db, Project *project)
{
    if (kind == SceneTemplate::Empty) return iris::SceneNodePtr();

    // THE FLOOR (WORLD-MODEL-1): an ORDINARY cube, exactly what the Add menu
    // makes (the shipped, baked cube primitive — so it has its LOD chain, its
    // cards and its SDF, and Atom draws it), scaled to 100 x 1 x 100 m with
    // its TOP face at y = 0, wearing the default floor material. It is
    // re-materialable, movable and deletable like any node, and it SHIPS LOCKED
    // (owner, 2026-09-15, restated 2026-09-30: not pickable, so a click on the
    // empty floor selects nothing and a drop on it is refused by name until the
    // user unlocks it in the outliner); `defaultFloor`
    // only says which material `material.reset` brings back and what the
    // Player's "hide the floor" setting hides. It casts no shadow (nothing is
    // under it, and a 100 m caster would widen the sun's fit to the whole
    // floor) and is a static box to physics.
    // THE TILE IS PINNED ONCE PER TEMPLATE (one import-pipeline visit, not one
    // per floor: World has 25). Each floor still gets its OWN material instance
    // — they could share one, but then editing one floor's material would edit
    // all 25, and a floor is an ordinary node; the decode buckets by shader words
    // anyway, so sharing would buy no draw.
    QString tileGuid;
    const QString tilePath = defaultfloormaterial::pinTile(db, project, &tileGuid);
    auto makeFloor = [db, project, &tileGuid, &tilePath](const QString &name, const iris::Vec3 &centre) {
        const QString guid = GUIDManager::generateGUID();
        iris::MeshNodePtr node = SceneNodeHelper::createBasicMeshNode(
            QStringLiteral(":/content/primitives/cube.obj"), name, guid, db);
        // cube.obj is a 2 m cube about its centre.
        node->setLocalScale(iris::Vec3(scenetemplate::kFloorSize * 0.5f,
                                       scenetemplate::kFloorThickness * 0.5f,
                                       scenetemplate::kFloorSize * 0.5f));
        node->setLocalPos(centre + iris::Vec3(0, -scenetemplate::kFloorThickness * 0.5f, 0));
        node->setShadowCastingEnabled(false);
        node->setPickable(false);          // ships LOCKED (above)
        node->defaultFloor = true;
        iris::PhysicsProperty physics;
        physics.objectMass = 0.0f;
        physics.isStatic = true;
        physics.objectCollisionMargin = 0.1f;
        physics.objectRestitution = 0.01f;
        physics.type = iris::PhysicsType::Static;
        physics.shape = iris::PhysicsCollisionShape::Cube;
        node->isPhysicsBody = true;
        node->physicsProperty = physics;

        const bool realProject = db && project && !project->getProjectGuid().isEmpty();
        if (realProject) {
            // The node's own Object row, as addPrimitive writes for any cube.
            QJsonObject props;
            props.insert(QStringLiteral("type"), QStringLiteral("builtin"));
            db->createAssetEntry(guid, name, static_cast<int>(ModelTypes::Object),
                                 project->getProjectGuid(), project->getProjectGuid(),
                                 QString(), QString(), QByteArray(),
                                 QJsonDocument(props).toJson(), QByteArray(), QByteArray());
        }
        node->setMaterial(defaultfloormaterial::createUnpinned(tilePath));
        if (realProject && !tileGuid.isEmpty())
            db->createDependency(static_cast<int>(ModelTypes::Object),
                                 static_cast<int>(ModelTypes::Texture), guid, tileGuid,
                                 project->getProjectGuid());
        return node;
    };

    if (kind == SceneTemplate::World) {
        // WORLD: twenty-five Basic floors, 5 x 5, edge to edge, centred on the
        // origin — a 500 m square standing in for terrain (Terra later). 500 m
        // is exactly the dynamic shadows' reach (OgreEngine's shadow far).
        auto group = iris::SceneNode::create();
        group->setName(QStringLiteral("World Floor"));
        group->setPickable(false);         // locked like the floors it holds
        const int n = scenetemplate::kWorldTilesPerSide;
        const float s = scenetemplate::kFloorSize;
        int index = 0;
        for (int row = 0; row < n; ++row) {
            for (int col = 0; col < n; ++col) {
                const iris::Vec3 centre((col - (n - 1) * 0.5f) * s, 0.0f,
                                        (row - (n - 1) * 0.5f) * s);
                group->addChild(makeFloor(QStringLiteral("Floor %1").arg(++index), centre));
            }
        }
        return group;
    }
    return makeFloor(QStringLiteral("Floor"), iris::Vec3(0, 0, 0));
}

iris::ScenePtr build(SceneTemplate kind, Database *db, Project *project)
{
    auto scene = iris::Scene::create();
    // New scenes start on EPIC (POST_CHAIN_SPEC.md §12 decision 8, owner call).
    // Applied through the registry rather than by hardcoding the values here, so
    // the tier table stays the single place any of them is written. Every
    // template, Empty included.
    worldmodes::setMode(scene, worldmodes::Mode::Epic);

    // EMPTY IS NOTHING (WORLD-MODEL-1, services/scenetemplate.h): the root, the
    // tier, no lights, no floor and NO SKY — SkyType::NONE (SKY-ATMOSPHERE-1):
    // a black background, nothing captured, no light, no reflection, no ambient
    // from a sky. It stops here, before anything is added: a user who asks for
    // empty gets a document a script would have built.
    if (kind == SceneTemplate::Empty) {
        scene->skyType = iris::SkyType::NONE;
        return scene;
    }

    // THE FLOOR (buildFloor above) — one "Floor", or World's 5 x 5 group.
    scene->rootNode->addChild(buildFloor(kind, db, project));

    auto dlight = iris::LightNode::create();
    dlight->setLightType(iris::LightType::Directional);
    scene->rootNode->addChild(dlight);
    dlight->setName("Directional Light");
    dlight->setLocalPos(iris::Vec3(4, 4, 0));
    // THE SUN HIGH AND BEHIND THE DEFAULT CAMERA (SKY-DEFAULTS-1; Unreal's default
    // class): 50 degrees of elevation, and its azimuth the camera's back — the
    // editor camera stands at (0, 5, 14) looking at the origin, down -Z, so the
    // sun stands towards +Z and its light travels (0, -sin 50, -cos 50). The
    // floor in view is fully lit and the sky ahead is the deep-blue side, away
    // from the sun. A pitch of 40 degrees about X turns the light's -Y to that.
    dlight->setLocalRot(iris::Quat::fromEulerAngles(scenetemplate::kSunPitchDegrees, 0, 0));
    // Through the funnel: these run AFTER addChild, so the node is already in
    // the scene and a raw field write is a change nothing reports
    // (SPECS/DIRTY_SET_MIRROR_SPEC.md; lead review R2 #10). The first sync of a
    // new scene is a full walk, so nothing depends on it today — which is
    // exactly why it would rot silently.
    dlight->setPropertyValue(QStringLiteral("intensity"), 1.0f);
    dlight->icon = iris::Texture2D::load(":/icons/light.png");
    dlight->markChanged(iris::NodeChange::Params);

    // THE SKY LIGHT (SKY_LIGHT_SPEC.md §2, owner decision §188d). A NEW SCENE IS
    // TWO LIGHTS: the sun above, and the sky's own fill. Delete both and the
    // scene is black — which is the whole point of ambient being a light.
    // (The "Point Light" that used to stand here is GONE, §5: it was a second
    // key light in a scene that needed a skylight, and it is exactly what made
    // "ambient" look like a thing a scene did not need.)
    auto skylight = iris::LightNode::create();
    skylight->setLightType(iris::LightType::Sky);
    scene->rootNode->addChild(skylight);
    skylight->setName("Sky Light");
    // WHERE THE POINT LIGHT STOOD. A Sky Light has no position — it is the sky —
    // but its ICON does, and an icon at the world origin sits exactly where the
    // default camera looks and on top of whatever a user drops there first. The
    // old template's second light stood at (-4, 4, 0); the marker for the light
    // that replaces it stands in the same place.
    skylight->setLocalPos(iris::Vec3(-4, 4, 0));
    skylight->setPropertyValue(QStringLiteral("intensity"), 1.0f);
    skylight->setPropertyValue(QStringLiteral("lightColor"), QColor(255, 255, 255));
    skylight->icon = iris::Texture2D::load(":/icons/light.png");
    skylight->markChanged(iris::NodeChange::Params);

    // THE DEFAULT SKY IS THE REAL ONE (owner answer Q1, 2026-09-18: "the
    // default new scene = the realistic real-time sky WITH the sun following
    // it"). The planet's atmosphere is drawn on the GPU (SKY-ATMOSPHERE-1),
    // it takes its sun — direction and light — from the scene's sun, the
    // directional light above, which is why the light is created first, and
    // the Sky Light integrates it for the scene's ambient. Sun
    // Follows Atmosphere needs no line here: LightNode::followsAtmosphere is
    // TRUE by default, and this is the sky that makes it mean something (on a
    // picked colour the tint is white and the row says so). Its dials are
    // SkyRealistic::defaults(), written through the one setter so the typed
    // fields and the JSON half cannot disagree.
    //
    // BOTH SELFTEST HASHES MOVE WITH THIS, by design: the self-test renders
    // this template, and the template's backdrop and ambient are now an
    // atmosphere instead of a flat 96-grey.
    scene->skyType = iris::SkyType::REALISTIC;
    scene->setSkyRealistic(iris::SkyRealistic::defaults());
    // The picked sky COLOUR stays what it was: it is what the World panel
    // shows the moment a user switches the sky back to Single Color, and the
    // fog colour reads from it (96 grey — owner pick 1, SKY_LIGHT_SPEC §9.1
    // option ii: srgb(96) decoded and integrated over the hemisphere is 0.117
    // of radiance against the old flat path's 0.120).
    scene->skyColor = QColor(96, 96, 96);
    scene->fogColor = QColor(96, 96, 96);
    scene->shadowEnabled = true;
    // THE EXPONENTIAL HEIGHT FOG ON (SKY-DEFAULTS-1; the owner's Unreal Basic
    // level): the world's medium at iris::HeightFog's dials — Unreal's density
    // and falloff, from 100 m, so the floor is untouched and the far world, the
    // horizon and everything under it take the sky's own blue. The World fog
    // stays off.
    scene->heightFog = iris::HeightFog();
    scene->heightFog.enabled = true;

    return scene;
}

}   // namespace scenetemplate
