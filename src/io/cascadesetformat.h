/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#pragma once
// THE SCENE'S PINNED CASCADE TABLE, READ (PHOTON-II-1, the merge read's worth-a-look 4).
// `giCascadeSet` is one [halfSize, resolution, stepCells] row per cascade, innermost
// first. The renderer's slot table holds kGiTierMaxCascades rows and world.gi refuses
// more (CASCADE-CAP-1); a FILE can still carry more, and the mirror used to truncate it
// silently. The reader keeps the first kGiTierMaxCascades rows and returns how many it
// dropped, so the open can say so as a scene issue (`gi.cascades.clamped`). Forward-only:
// no compatibility arm — the extra rows are not kept anywhere.
#include <QJsonArray>
#include <QVector>

#include "irisgl/core/math/vec.h"
#include "jahshaka/engine/Types.h"

namespace sceneformat {

/// Reads `rows` into `out` (cleared first); rows with fewer than three numbers are not
/// rows. Returns how many well-formed rows lay past the renderer's ceiling and were
/// dropped.
inline int readCascadeSet(const QJsonArray &rows, QVector<iris::Vec3> &out)
{
    out.clear();
    int dropped = 0;
    for (const QJsonValue &v : rows) {
        const QJsonArray row = v.toArray();
        if (row.size() < 3) continue;
        if (out.size() >= jahshaka::engine::kGiTierMaxCascades) { ++dropped; continue; }
        out.append(iris::Vec3(float(row.at(0).toDouble()), float(row.at(1).toDouble()),
                              float(row.at(2).toDouble())));
    }
    return dropped;
}

}   // namespace sceneformat
