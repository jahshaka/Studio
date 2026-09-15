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
#include "services/sceneeditservice.h"
#include "shell/mainwindow.h"

Database::Database() {}
Database::~Database() {}

AssetRecord Database::fetchAsset(const QString &) { return AssetRecord(); }
bool Database::addFavorite(const QString &) { return false; }
bool Database::removeFavorite(const QString &) { return false; }

void MainWindow::applyMaterialPreset(MaterialPreset) {}

void SceneEditService::addPrimitive(const QString &, const std::optional<iris::Vec3> &,
                                    surfaceplacement::Placement)
{
}
