/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// Link stubs for ui.presets_favorites (the tests/ui idiom). The two presets
// panels reach the library, the editor's add-primitive verb and MainWindow's
// material-preset action — all of them only from context menus and
// double-clicks this suite never performs. The library READS the suite does
// care about (fetchFavorites, fetchObjectMesh) are faked in the test itself,
// where their contents are part of the assertions.

#include "data/database/database.h"
#include "data/materialpreset.h"
#include "services/materialpresetassets.h"
#include "services/materialpresetseeder.h"
#include "services/sceneeditservice.h"
#include "shell/mainwindow.h"

Database::Database() {}
Database::~Database() {}

AssetRecord Database::fetchAsset(const QString &) { return AssetRecord(); }
bool Database::addFavorite(const QString &) { return false; }
bool Database::removeFavorite(const QString &) { return false; }

void SceneEditService::addPrimitive(const QString &, const std::optional<iris::Vec3> &,
                                    surfaceplacement::Placement)
{
}

// The tray's double-click asks the ONE apply (MATERIAL-PREVIEW-1). The suite
// never double-clicks — and hands the panels no services — so the call is
// never made; the symbol still has to resolve.
bool SceneEditService::applyMaterial(const QString &, iris::SceneNodePtr)
{
    return false;
}

// The tray's Customise (R18) mints a material through the ONE preset service.
// Like the apply above it is a context-menu gesture this suite never performs,
// and the service reaches the library, the store and the import pipeline — so
// the symbols resolve here rather than dragging half of services/ into a
// panel-layout suite. What Customise DOES is asserted where it is
// implemented: materials.bundle and scripting.e2e.preset_apply.
void SceneEditService::requestAssetViewRefresh() {}

// The tray's Customise stands the launch seeder down before it mints (one
// importer at a time). This suite never opens a context menu and never starts
// a seeder, so the singleton is a stub here like the service beside it.
MaterialPresetSeeder &MaterialPresetSeeder::instance()
{
    static MaterialPresetSeeder *seeder = new MaterialPresetSeeder();
    return *seeder;
}
void MaterialPresetSeeder::finishNow() {}

namespace MaterialPresetAssets {
QString guidFor(const QString &) { return QString(); }
QString customise(const QString &, const QString &, Database *, Project *, QString *)
{
    return QString();
}
}

// The tray's Customise also asks for the new material's TILE — a render of it
// on the studio sphere through the one shared renderer (MATPREVIEW-ENV-1,
// owner review R9(a)). Same reasoning as the service above: a context-menu
// gesture this layout suite never performs, whose implementation would drag
// the engine into a panel test. What the render DOES is asserted in
// thumbnails.studio_env.
#include "bridge/enginehost.h"
#include "services/thumbnailrebuild.h"

EngineHost &EngineHost::instance()
{
    // A REAL object, never destroyed (its destructor stops the engine, which
    // this suite has none of): `engine()` on it answers null, which is what
    // the render request is given and what it refuses by name.
    static EngineHost *host = new EngineHost();
    return *host;
}

namespace thumbrebuild {
Outcome rebuildOne(Database *, Project *, const QString &,
                   const std::shared_ptr<jahshaka::engine::Engine> &)
{
    return Outcome::bad(QStringLiteral("no engine in this test"));
}
}
