/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef TESTS_AVATAR_AVATARFIXTURES_H
#define TESTS_AVATAR_AVATARFIXTURES_H

// THE AVATAR SUITES' FIXTURES, BAKED (SHIPPED-BAKES-1).
//
// The preview model reads a character from its BAKE and a clip from its CLIP
// bake — it never parses. These suites have no library behind them, so the
// fixture files are baked here, in-process, by the same builders an import
// runs (MeshBake::buildFromFile / buildClipFromFile): the test pays the parse,
// the subject under test reads the product.

#include <QString>
#include <memory>

#include "irisgl/import/meshbake.h"
#include "modules/avatar/avatarpreviewmodel.h"

namespace avatarfixture
{

inline avatar::AvatarPreviewModel::SubjectSource subject(const QString &path)
{
    avatar::AvatarPreviewModel::SubjectSource source;
    source.path = path;
    iris::MeshBake::Model model = iris::MeshBake::buildFromFile(path, QStringLiteral("fixture"));
    if (model.valid) source.baked = std::make_shared<const iris::MeshBake::Model>(std::move(model));
    return source;
}

inline iris::MeshBake::Clip clip(const QString &path)
{
    return iris::MeshBake::buildClipFromFile(path, QStringLiteral("fixture"));
}

}   // namespace avatarfixture

#endif   // TESTS_AVATAR_AVATARFIXTURES_H
